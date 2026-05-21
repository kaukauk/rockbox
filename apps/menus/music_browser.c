/* music_browser.c — see music_browser.h
 *
 * Three nested gui_synclist screens, each one populated by a fresh
 * tagcache_search.  We capture the result_seek alongside each name so
 * the next level down can filter by it (artist seek → album seek →
 * title), and at the track level we also retrieve the filename so the
 * displayed list and the resulting playlist are guaranteed to be in
 * the same order — otherwise a sorted display + an unsorted playlist
 * walk lands you on the wrong track.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "system.h"
#include "lang.h"
#include "action.h"
#include "list.h"
#include "splash.h"
#include "tagcache.h"
#include "playlist.h"
#include "audio.h"
#include "settings.h"
#include "root_menu.h"
#include "music_browser.h"

#define MUSIC_MAX_ENTRIES   1024
#define MUSIC_MAX_NAME_LEN  64
#define MUSIC_UNIQBUF_SIZE  (64 * 1024)

struct music_entry {
    char name[MUSIC_MAX_NAME_LEN];
    int  seek;            /* tcs.result_seek — feed as filter to next level */
    char path[MAX_PATH];  /* filled only at the track level (filename) */
};

/* All three levels share these buffers — only one is live at a time
 * because we navigate strictly depth-first.  ~270 KB of BSS total
 * (1024 × ~270 bytes) — fine on the Y1 (256 MB host RAM). */
static struct music_entry s_entries[MUSIC_MAX_ENTRIES];
static int  s_count;

/* tagcache_search needs a uniqbuf to deduplicate hits — without it
 * every track in an album yields a duplicate album entry (the
 * underlying iteration walks filename rows, not distinct tag values).
 */
static uint32_t s_uniqbuf[MUSIC_UNIQBUF_SIZE / sizeof(uint32_t)];

/* Current browse level, used by the icon callback to decide whether a
 * row should render as a folder (artist/album) or a track. */
enum mb_level { MB_LV_ARTIST = 0, MB_LV_ALBUM, MB_LV_TRACK };
static enum mb_level s_level;

/* ---------- gui_synclist callbacks ---------- */
static const char *mb_get_name(int item, void *data,
                               char *buf, size_t buflen)
{
    (void)data; (void)buf; (void)buflen;
    if (item < 0 || item >= s_count)
        return "";
    return s_entries[item].name;
}

static enum themable_icons mb_get_icon(int item, void *data)
{
    (void)item; (void)data;
    return (s_level == MB_LV_TRACK) ? Icon_Audio : Icon_Folder;
}

/* ---------- sorting ---------- */
static int mb_cmp(const void *a, const void *b)
{
    const struct music_entry *ea = a, *eb = b;
    return strcasecmp(ea->name, eb->name);
}

/* ---------- populate ----------
 * Run tagcache_search(tag) with up to two seek filters and store the
 * results into s_entries[].  Returns false on tagcache failure.
 *
 * `untagged_label`: if non-NULL, encountering an "<Untagged>" hit is
 * tracked and a synthetic catch-all entry with this label is appended
 * to the END of the (sorted) list, carrying the untagged seek so the
 * next level can filter on it.  Used for "[No Artist]" / "[No Album]".
 *
 * `fetch_paths`: at the track level we also retrieve the row's
 * filename into entry.path so play_album() can build the playlist in
 * the exact same display order — without this the sort below would
 * desync the user's tap from playback. */
static bool populate(int tag,
                     int filter_tag_1, int filter_seek_1,
                     int filter_tag_2, int filter_seek_2,
                     const char *untagged_label,
                     bool fetch_paths)
{
    struct tagcache_search tcs;
    s_count = 0;
    int  untagged_seek    = 0;
    bool untagged_present = false;

    if (!tagcache_search(&tcs, tag))
        return false;

    tagcache_search_set_uniqbuf(&tcs, s_uniqbuf, MUSIC_UNIQBUF_SIZE);

    if (filter_tag_1 >= 0)
        tagcache_search_add_filter(&tcs, filter_tag_1, filter_seek_1);
    if (filter_tag_2 >= 0)
        tagcache_search_add_filter(&tcs, filter_tag_2, filter_seek_2);

    while (s_count < MUSIC_MAX_ENTRIES &&
           tagcache_get_next(&tcs, s_entries[s_count].name,
                             MUSIC_MAX_NAME_LEN))
    {
        const char *n = s_entries[s_count].name;
        if (n[0] == '\0' || strcmp(n, UNTAGGED) == 0)
        {
            /* Defer untagged to the end as a single catch-all bucket
             * — we only need to capture the seek once; collapsing N
             * untagged hits is what uniqbuf does for us above. */
            untagged_seek    = tcs.result_seek;
            untagged_present = true;
            continue;
        }

        s_entries[s_count].seek = tcs.result_seek;

        if (fetch_paths)
        {
            /* tcs.idx_id is the row id of the currently-yielded entry.
             * tagcache_retrieve looks up other tags for that same row,
             * including its filename. */
            if (!tagcache_retrieve(&tcs, tcs.idx_id, tag_filename,
                                   s_entries[s_count].path,
                                   sizeof(s_entries[s_count].path)))
            {
                s_entries[s_count].path[0] = '\0';
            }
        }

        s_count++;
    }

    tagcache_search_finish(&tcs);

    qsort(s_entries, s_count, sizeof(s_entries[0]), mb_cmp);

    /* Append the untagged catch-all after the sort, so it sits at the
     * bottom of the list rather than getting interleaved alphabetically
     * with the user's real artists/albums. */
    if (untagged_present && untagged_label
        && s_count < MUSIC_MAX_ENTRIES)
    {
        strlcpy(s_entries[s_count].name, untagged_label,
                sizeof(s_entries[s_count].name));
        s_entries[s_count].seek = untagged_seek;
        s_entries[s_count].path[0] = '\0';
        s_count++;
    }

    return s_count > 0;
}

/* ---------- list loop ----------
 * Runs a synclist with the populated s_entries[].  Returns:
 *   >= 0  → index user selected (with ACTION_STD_OK)
 *   -1    → user cancelled (ACTION_STD_CANCEL)
 *   -2    → USB attached / other system exit
 */
static int run_list(const char *title)
{
    struct gui_synclist list;
    gui_synclist_init(&list, mb_get_name, NULL, false, 1, NULL);
    gui_synclist_set_icon_callback(&list, mb_get_icon);
    gui_synclist_set_nb_items(&list, s_count);
    gui_synclist_set_title(&list, (char *)title,
                           s_level == MB_LV_ARTIST ? Icon_Audio :
                           s_level == MB_LV_ALBUM  ? Icon_Folder : Icon_Audio);
    gui_synclist_draw(&list);

    int action;
    while (1)
    {
        list_do_action(CONTEXT_LIST, HZ / 2, &list, &action);
        switch (action)
        {
            case ACTION_STD_OK:
                return gui_synclist_get_sel_pos(&list);
            case ACTION_STD_CANCEL:
                return -1;
            case ACTION_STD_MENU:
            case SYS_USB_CONNECTED:
                return -2;
        }
    }
}

/* ---------- playback ----------
 * Build a playlist out of the just-displayed track list (which is
 * already sorted by title) and start at the index the user chose.
 * Iterating s_entries[] directly is what keeps the display index in
 * sync with the playlist position — re-walking the tagcache here would
 * almost certainly yield a different order. */
static bool play_album(int start_idx)
{
    if (s_count == 0)
        return false;

    if (playlist_create(NULL, NULL) != 0)
    {
        splash(HZ, "Playlist init failed");
        return false;
    }

    int inserted = 0;
    for (int i = 0; i < s_count; i++)
    {
        if (!s_entries[i].path[0])
            continue;
        if (playlist_insert_track(NULL, s_entries[i].path,
                                  PLAYLIST_INSERT_LAST, false, false) < 0)
            break;
        inserted++;
    }

    if (inserted == 0)
    {
        splash(HZ, "No tracks");
        return false;
    }

    if (start_idx >= inserted)
        start_idx = 0;

    playlist_start(start_idx, 0, 0);
    return true;
}

/* ---------- main entry ---------- */
int music_browse(void *param)
{
    (void)param;

    /* Need a usable tagcache, otherwise abort gracefully. */
    if (!tagcache_is_usable())
    {
        splash(HZ * 2, ID2P(LANG_TAGCACHE_BUSY));
        return GO_TO_PREVIOUS;
    }

    while (1) /* artist loop */
    {
        s_level = MB_LV_ARTIST;
        if (!populate(tag_artist, -1, 0, -1, 0,
                      "[No Artist]", false))
        {
            splash(HZ * 2, ID2P(LANG_NOTHING_TO_RESUME));
            return GO_TO_PREVIOUS;
        }
        int artist_idx = run_list(str(LANG_MUSIC));
        if (artist_idx == -2) return GO_TO_WPS;        /* USB / menu */
        if (artist_idx <  0) return GO_TO_PREVIOUS;

        char artist_name[MUSIC_MAX_NAME_LEN];
        int  artist_seek = s_entries[artist_idx].seek;
        strlcpy(artist_name, s_entries[artist_idx].name, sizeof(artist_name));

        while (1) /* album loop */
        {
            s_level = MB_LV_ALBUM;
            if (!populate(tag_album, tag_artist, artist_seek, -1, 0,
                          "[No Album]", false))
            {
                splash(HZ, "No albums");
                break;
            }
            int album_idx = run_list(artist_name);
            if (album_idx == -2) return GO_TO_WPS;
            if (album_idx <  0) break;

            char album_name[MUSIC_MAX_NAME_LEN];
            int  album_seek = s_entries[album_idx].seek;
            strlcpy(album_name, s_entries[album_idx].name, sizeof(album_name));

            while (1) /* track loop */
            {
                s_level = MB_LV_TRACK;
                if (!populate(tag_title,
                              tag_artist, artist_seek,
                              tag_album,  album_seek,
                              NULL, true))
                {
                    splash(HZ, "No tracks");
                    break;
                }
                int track_idx = run_list(album_name);
                if (track_idx == -2) return GO_TO_WPS;
                if (track_idx <  0) break;

                if (play_album(track_idx))
                    return GO_TO_WPS;
                /* else stay on the track list */
            }
        }
    }
}

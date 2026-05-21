/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2007 Jonathan Gordon
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "string-extra.h"
#include "config.h"
#include "appevents.h"
#include "menu.h"
#include "root_menu.h"
#include "lang.h"
#include "settings.h"
#include "screens.h"
#include "kernel.h"
#include "debug.h"
#include "misc.h"
#include "open_plugin.h"
#include "rolo.h"
#include "powermgmt.h"
#include "power.h"
#include "talk.h"
#include "audio.h"
#include "shortcuts.h"
#include "action.h"
#include "yesno.h"
#include "splash.h"
#include "button.h"

#ifdef HAVE_HOTSWAP
#include "storage.h"
#include "mv.h"
#endif
/* gui api */
#include "list.h"
#include "splash.h"
#include "action.h"
#include "yesno.h"
#include "viewport.h"

#include "tree.h"
#if CONFIG_TUNER
#include "radio.h"
#endif
#ifdef HAVE_RECORDING
#include "recording.h"
#endif
#include "wps.h"
#include "bookmark.h"
#include "playlist.h"
#include "playlist_viewer.h"
#include "playlist_catalog.h"
#include "menus/exported_menus.h"
#ifdef HAVE_RTC_ALARM
#include "rtc.h"
#endif
#ifdef HAVE_TAGCACHE
#include "tagcache.h"
#endif
#include "language.h"
#include "plugin.h"
#include "disk.h"

struct root_items {
    int (*function)(void* param);
    void* param;
    const struct menu_item_ex *context_menu;
};
static int next_screen = GO_TO_ROOT; /* holding info about the upcoming screen
                                        * which is the current screen for the
                                        * rest of the code after load_screen
                                        * is called */
static int last_screen = GO_TO_ROOT; /* unfortunatly needed so we can resume
                                        or goto current track based on previous
                                        screen */

static int previous_music = GO_TO_WPS; /* Toggles behavior of the return-to
                                        * playback-button depending
                                        * on FM radio */

#if (CONFIG_TUNER)
static void rootmenu_start_playback_callback(unsigned short id, void *param)
{
    (void) id; (void) param;
    /* Cancel FM radio selection as previous music. For cases where we start
       playback without going to the WPS, such as playlist insert or
       playlist catalog. */
    previous_music = GO_TO_WPS;
}
#endif

static char current_track_path[MAX_PATH];
static void rootmenu_track_changed_callback(unsigned short id, void* param)
{
    (void)id;
    struct mp3entry *id3 = ((struct track_event *)param)->id3;
    strmemccpy(current_track_path, id3->path, MAX_PATH);
}
static int browser(void* param)
{
    int ret_val;
#ifdef HAVE_TAGCACHE
    struct tree_context* tc = tree_get_context();
#endif
    int filter = SHOW_SUPPORTED;
    char folder[MAX_PATH] = "/";
    /* stuff needed to remember position in file browser */
    /*static char last_folder[MAX_PATH] = "/"*/
    char *last_folder = global_status.browse_last_folder;

    /* and stuff for the database browser */
#ifdef HAVE_TAGCACHE
    static int last_db_dirlevel = 0, last_db_selection = 0, last_ft_dirlevel = 0;
#endif

    int initial_descend = 0;
    switch ((intptr_t)param)
    {
        case GO_TO_FILEBROWSER:
            filter = global_settings.dirfilter;
            if (global_settings.browse_current &&
                    last_screen == GO_TO_WPS &&
                    current_track_path[0])
            {
                strcpy(folder, current_track_path);
            }
            else if (!strcmp(last_folder, "/"))
            {
                strcpy(folder, global_settings.start_directory);
            }
            else
            {
#ifdef HAVE_HOTSWAP
                bool in_hotswap = false;
                /* handle entering an ejected drive */
                int i;
                for (i = 0; i < NUM_VOLUMES; i++)
                {
                    char vol_string[VOL_MAX_LEN + 1];
                    if (!volume_removable(i))
                        continue;
                    get_volume_name(i, vol_string);
                    /* test whether we would browse the external card */
                    if (!volume_present(i) &&
                            (strstr(last_folder, vol_string)
#ifdef HAVE_HOTSWAP_STORAGE_AS_MAIN
                                                                || (i == 0)
#endif
                                                                ))
                    {   /* leave folder as "/" to avoid crash when trying
                         * to access an ejected drive */
                        strcpy(folder, "/");
                        in_hotswap = true;
                        break;
                    }
                }
                if (!in_hotswap)
#endif /*HAVE_HOTSWAP*/
                    strcpy(folder, last_folder);
            }
            push_current_activity(ACTIVITY_FILEBROWSER);
        break;
#ifdef HAVE_TAGCACHE
        case GO_TO_DBBROWSER:
            if (!tagcache_is_usable())
            {
                bool reinit_attempted = false;

                /* Now display progress until it's ready or the user exits */
                while(!tagcache_is_usable())
                {
                    struct tagcache_stat *stat = tagcache_get_stat();

                    /* Allow user to exit */
                    if (action_userabort(HZ/2))
                        break;

                    /* Maybe just needs to reboot due to delayed commit */
                    if (stat->commit_delayed)
                    {
                        #ifdef INNIOASIS_Y1
                                splash(HZ, "Restarting Rockbox to apply...");
                                list_stop_handler();
                                sleep(1);

                                system("am force-stop org.rockbox");
                                system("monkey -p org.rockbox -c android.intent.category.LAUNCHER 1");
                        #else
                                splash(HZ, ID2P(LANG_PLEASE_REBOOT));
                        #endif
                        break;
                    }

                    /* Check if ready status is known */
                    if (!stat->readyvalid)
                    {
                        splash(0, ID2P(LANG_TAGCACHE_BUSY));
                        continue;
                    }

                    /* Re-init if required */
                    if (!reinit_attempted && !stat->ready &&
                        stat->processed_entries == 0 && stat->commit_step == 0)
                    {
                        /* Prompt the user */
                        reinit_attempted = true;
                        static const char *lines[]={
                            ID2P(LANG_TAGCACHE_BUSY), ID2P(LANG_TAGCACHE_FORCE_UPDATE)};
                        static const struct text_message message={lines, 2};
                        if(gui_syncyesno_run(&message, NULL, NULL) == YESNO_NO)
                            break;
                        FOR_NB_SCREENS(i)
                            screens[i].clear_display();

                        /* Start initialisation */
                        tagcache_rebuild();
                    }

                    /* Display building progress */
                    static long talked_tick = 0;
                    if(global_settings.talk_menu &&
                       (talked_tick == 0
                        || TIME_AFTER(current_tick, talked_tick+7*HZ)))
                    {
                        talked_tick = current_tick;
                        if (stat->commit_step > 0)
                        {
                            talk_id(LANG_TAGCACHE_INIT, false);
                            talk_number(stat->commit_step, true);
                            talk_id(VOICE_OF, true);
                            talk_number(tagcache_get_max_commit_step(), true);
                        } else if(stat->processed_entries)
                        {
                            talk_number(stat->processed_entries, false);
                            talk_id(LANG_BUILDING_DATABASE, true);
                        }
                    }
                    if (stat->commit_step > 0)
                    {
                        /* (prevent redundant voicing by splash_progress */
                        bool tmp = global_settings.talk_menu;
                        global_settings.talk_menu = false;

                        if (lang_is_rtl())
                        {
                            splash_progress(stat->commit_step,
                                            tagcache_get_max_commit_step(),
                                            "[%d/%d] %s", stat->commit_step,
                                            tagcache_get_max_commit_step(),
                                            str(LANG_TAGCACHE_INIT));
                        }
                        else
                        {
                            splash_progress(stat->commit_step,
                                            tagcache_get_max_commit_step(),
                                            "%s [%d/%d]", str(LANG_TAGCACHE_INIT),
                                            stat->commit_step,
                                            tagcache_get_max_commit_step());
                        }
                        global_settings.talk_menu = tmp;
                    }
                    else
                    {
                        splashf(0, str(LANG_BUILDING_DATABASE),
                                   stat->processed_entries); /* (voiced above) */
                    }
                }
            }
            if (!tagcache_is_usable())
                return GO_TO_PREVIOUS;
            filter = SHOW_ID3DB;
            last_ft_dirlevel = tc->dirlevel;
            tc->dirlevel = last_db_dirlevel;
            tc->selected_item = last_db_selection;
            push_current_activity(ACTIVITY_DATABASEBROWSER);
        break;
        case GO_TO_MUSIC:
            if (!tagcache_is_usable())
                return GO_TO_PREVIOUS;
            filter = SHOW_ID3DB;
            last_ft_dirlevel = tc->dirlevel;
            tc->dirlevel = 0;
            tc->selected_item = 0;
            initial_descend = 1; /* tagnavi "main": index 1 = Artist */
            push_current_activity(ACTIVITY_DATABASEBROWSER);
        break;
#endif /*HAVE_TAGCACHE*/
    }

    struct browse_context browse = {
        .dirfilter = filter,
        .icon = Icon_NOICON,
        .root = folder,
#ifdef HAVE_TAGCACHE
        .initial_descend = initial_descend,
#endif
    };

    ret_val = rockbox_browse(&browse);

    if (ret_val == GO_TO_WPS
        || ret_val == GO_TO_PREVIOUS_MUSIC
        || ret_val == GO_TO_PLUGIN)
        pop_current_activity_without_refresh();
    else
        pop_current_activity();

    switch ((intptr_t)param)
    {
        case GO_TO_FILEBROWSER:
            if (!get_current_file(last_folder, MAX_PATH) ||
                (!strchr(&last_folder[1], '/') &&
                 global_settings.start_directory[1] != '\0'))
            {
                last_folder[0] = '/';
                last_folder[1] = '\0';
            }
        break;
#ifdef HAVE_TAGCACHE
        case GO_TO_DBBROWSER:
            last_db_dirlevel = tc->dirlevel;
            last_db_selection = tc->selected_item;
            tc->dirlevel = last_ft_dirlevel;
        break;
#endif
    }
    return ret_val;
}

#ifdef HAVE_RECORDING
static int recscrn(void* param)
{
    (void)param;
    recording_screen(false);
    return GO_TO_ROOT;
}
#endif
static int wpsscrn(void* param)
{
    int ret_val = GO_TO_PREVIOUS;
    int audstatus = audio_status();
    (void)param;
    push_current_activity(ACTIVITY_WPS);

#ifdef HAVE_PITCHCONTROL
    if (!audstatus)
    {
        sound_set_pitch(global_status.resume_pitch);
        dsp_set_timestretch(global_status.resume_speed);
    }
#endif

    if (audstatus)
    {
        talk_shutup();
        ret_val = gui_wps_show();
    }
    else if (global_status.resume_index != -1)
    {
        DEBUGF("Resume index %d crc32 %lX offset %lX\n",
               global_status.resume_index,
               (unsigned long)global_status.resume_crc32,
               (unsigned long)global_status.resume_offset);
        if (playlist_resume() != -1)
        {
            playlist_resume_track(global_status.resume_index,
                global_status.resume_crc32,
                global_status.resume_elapsed,
                global_status.resume_offset);
            ret_val = gui_wps_show();
        }
    }
    else if (!file_exists(PLAYLIST_CONTROL_FILE))
        splash(HZ*2, ID2P(LANG_NOTHING_TO_RESUME));
    else if (yesno_pop(ID2P(LANG_REPLAY_FINISHED_PLAYLIST)) &&
             playlist_resume() != -1)
    {
        playlist_start(0, 0, 0);
        ret_val = gui_wps_show();
    }

    if (ret_val == GO_TO_PLAYLIST_VIEWER
        || ret_val == GO_TO_PLUGIN
        || ret_val == GO_TO_WPS
        || ret_val == GO_TO_PREVIOUS_MUSIC
        || ret_val == GO_TO_PREVIOUS_BROWSER
        || (ret_val == GO_TO_PREVIOUS
               && (last_screen == GO_TO_MAINMENU /* Settings */
                || last_screen == GO_TO_BROWSEPLUGINS
                || last_screen == GO_TO_SYSTEM_SCREEN
                || last_screen == GO_TO_PLAYLISTS_SCREEN)))
    {
        pop_current_activity_without_refresh();
    }
    else
        pop_current_activity();

    return ret_val;
}
#if CONFIG_TUNER
static int radio(void* param)
{
    (void)param;
    radio_screen();
    return GO_TO_ROOT;
}
#endif

static int miscscrn(void * param)
{
    const struct menu_item_ex *menu = (const struct menu_item_ex*)param;
    int result = do_menu(menu, NULL, NULL, false);
    switch (result)
    {
        case GO_TO_PLUGIN:
        case GO_TO_PLAYLIST_VIEWER:
        case GO_TO_WPS:
        case GO_TO_PREVIOUS_MUSIC:
            return result;
        default:
            return GO_TO_ROOT;
    }
}


static int playlist_view_catalog(void * param)
{
    (void)param;
    push_current_activity(ACTIVITY_PLAYLISTBROWSER);
    bool item_was_selected = catalog_view_playlists();

    if (item_was_selected)
    {
        pop_current_activity_without_refresh();
        return GO_TO_WPS;
    }
    pop_current_activity();
    return GO_TO_ROOT;
}

static int playlist_view(void * param)
{
    (void)param;
    int val;

    val = playlist_viewer();
    switch (val)
    {
        case PLAYLIST_VIEWER_MAINMENU:
        case PLAYLIST_VIEWER_USB:
            return GO_TO_ROOT;
        case PLAYLIST_VIEWER_OK:
            return GO_TO_PREVIOUS;
    }
    return GO_TO_PREVIOUS;
}

static int load_bmarks(void* param)
{
    (void)param;
    if(bookmark_mrb_load())
        return GO_TO_WPS;
    return GO_TO_PREVIOUS;
}

/* These are all static const'd from apps/menus/ *.c
   so little hack so we can use them */
extern struct menu_item_ex
        file_menu,
#ifdef HAVE_TAGCACHE
        tagcache_menu,
#endif
        main_menu_,
        manage_settings,
        plugin_menu,
        playlist_options,
        info_menu,
        system_menu;
#ifdef INNIOASIS_Y1
extern struct menu_item_ex fm_radio_app_item;
#endif
/* Defined further down — needed here so items[] can take its address. */
static int show_other_items(void *param);
static int browse_audiobooks(void *param);
static const struct root_items items[] = {
    [GO_TO_FILEBROWSER] =   { browser, (void*)GO_TO_FILEBROWSER, &file_menu},
#ifdef HAVE_TAGCACHE
    [GO_TO_DBBROWSER] =     { browser, (void*)GO_TO_DBBROWSER, &tagcache_menu },
#endif
    [GO_TO_WPS] =           { wpsscrn, NULL, &playback_settings },
    [GO_TO_MAINMENU] =      { miscscrn, (struct menu_item_ex*)&main_menu_,
                                                            &manage_settings },

#ifdef HAVE_RECORDING
    [GO_TO_RECSCREEN] =     {  recscrn, NULL, &recording_settings_menu },
#endif

#if CONFIG_TUNER
    [GO_TO_FM] =            { radio, NULL, &radio_settings_menu },
#endif

    [GO_TO_RECENTBMARKS] =  { load_bmarks, NULL, &bookmark_settings_menu },
    [GO_TO_BROWSEPLUGINS] = { miscscrn, &plugin_menu, NULL },
    [GO_TO_PLAYLISTS_SCREEN] = { playlist_view_catalog, NULL,
                                                        &playlist_options },
    [GO_TO_PLAYLIST_VIEWER] = { playlist_view, NULL, &playlist_options },
    [GO_TO_SYSTEM_SCREEN] = { miscscrn, &info_menu, &system_menu },
    [GO_TO_SHORTCUTMENU] = { do_shortcut_menu, NULL, NULL },
#ifdef INNIOASIS_Y1
    [GO_TO_FM_RADIO_APP] = { miscscrn, &fm_radio_app_item, NULL },
#endif
    [GO_TO_OTHER_ITEMS] = { show_other_items, NULL, NULL },
    [GO_TO_AUDIOBOOKS_BROWSE] = { browse_audiobooks, NULL, NULL },
#ifdef HAVE_TAGCACHE
#ifdef HAVE_TAGCACHE
    [GO_TO_MUSIC] = { browser, (void*)GO_TO_MUSIC, &tagcache_menu },
#endif
#endif

};
#define NUM_ITEMS (int)(sizeof(items)/sizeof(*items))

/* Exposed via root_menu.h so apps/menus/fm_radio_app.c can wire its
 * MENUITEM_FUNCTION to the same callback and participate in the
 * hide/unhide flow. */
int item_callback(int action,
                  const struct menu_item_ex *this_item,
                  struct gui_synclist *this_list);

/* "Other Items" — root-menu entry that opens the hidden-items submenu.
 * Declared up here so the helpers below can take its address. */
MENUITEM_RETURNVALUE(other_items, ID2P(LANG_OTHER_ITEMS), GO_TO_OTHER_ITEMS,
                     item_callback, Icon_Submenu);

/* --------------------------------------------------------------------- */
/* "Other Items" — user-managed hidden-items submenu                     */
/*                                                                       */
/* Storage: global_settings.main_menu_hidden, a comma-separated list of  */
/* menu_table[] string keys (e.g. "database,radio,playlists").  Empty by */
/* default → nothing hidden.                                             */
/* --------------------------------------------------------------------- */

static struct menu_table menu_table[]; /* fwd decl */

/* Return true iff `key` appears in main_menu_hidden as a comma-delimited
 * token. */
static bool main_menu_is_hidden(const char *key)
{
    const char *list = (const char *)global_settings.main_menu_hidden;
    if (!list || !list[0] || !key || !key[0])
        return false;
    size_t klen = strlen(key);
    const char *p = list;
    while ((p = strstr(p, key)) != NULL)
    {
        bool left_ok  = (p == list) || (p[-1] == ',');
        bool right_ok = (p[klen] == '\0') || (p[klen] == ',');
        if (left_ok && right_ok)
            return true;
        p++;
    }
    return false;
}

/* Append `key` to main_menu_hidden as a comma-delimited token (no-op
 * if the key is already present or the buffer would overflow). */
static void main_menu_add_hidden(const char *key)
{
    if (main_menu_is_hidden(key))
        return;
    char *list = (char *)global_settings.main_menu_hidden;
    size_t cap = sizeof(global_settings.main_menu_hidden);
    size_t cur = strlen(list);
    size_t klen = strlen(key);
    /* need: (cur ? cur + 1 : 0) for "<existing>," + klen + null */
    size_t need = cur + (cur ? 1 : 0) + klen + 1;
    if (need > cap)
        return; /* drop silently — list is only 256 bytes, plenty for menus */
    if (cur)
        list[cur++] = ',';
    memcpy(list + cur, key, klen + 1);
    settings_save();
}

/* Remove `key` from main_menu_hidden (no-op if absent). */
static void main_menu_remove_hidden(const char *key)
{
    char *list = (char *)global_settings.main_menu_hidden;
    if (!list[0])
        return;
    size_t klen = strlen(key);
    char *p = list;
    while ((p = strstr(p, key)) != NULL)
    {
        bool left_ok  = (p == list) || (p[-1] == ',');
        bool right_ok = (p[klen] == '\0') || (p[klen] == ',');
        if (left_ok && right_ok)
        {
            /* Take the trailing comma with the key if present;
             * otherwise (last token) take the leading comma instead. */
            size_t total = strlen(list);
            char *rm_start;
            char *rm_end;
            if (p[klen] == ',')
            {
                rm_start = p;
                rm_end   = p + klen + 1;
            }
            else if (p > list && p[-1] == ',')
            {
                rm_start = p - 1;
                rm_end   = p + klen;
            }
            else
            {
                /* only token in the list */
                rm_start = p;
                rm_end   = p + klen;
            }
            memmove(rm_start, rm_end, total - (rm_end - list) + 1);
            settings_save();
            return;
        }
        p++;
    }
}

/* Lookup the menu_table[] key string for a given menu_item_ex pointer.
 * NULL if not in menu_table (e.g. dynamically allocated items). */
static const char *main_menu_key_for_item(const struct menu_item_ex *item);

/* Pop "Move to Other Items?" Yes/No.  Returns true if user said Yes. */
static bool confirm_hide_prompt(void)
{
    static const char *lines[]   = { ID2P(LANG_MAINMENU_HIDE_PROMPT) };
    static const struct text_message message = { lines, 1 };
    return gui_syncyesno_run(&message, NULL, NULL) == YESNO_YES;
}

static bool confirm_unhide_prompt(void)
{
    static const char *lines[]   = { ID2P(LANG_MAINMENU_UNHIDE_PROMPT) };
    static const struct text_message message = { lines, 1 };
    return gui_syncyesno_run(&message, NULL, NULL) == YESNO_YES;
}

/* True if `action` arrived via short-press BUTTON_LEFT specifically (and
 * not via, say, BUTTON_PLAY|BUTTON_REPEAT, which on Y1 also produces
 * ACTION_STD_CANCEL).  Used to gate the hide/unhide flow so a stray
 * long-press play in the root menu doesn't dump items into "Other". */
static bool action_came_from_left_button(void)
{
    int btn = 0;
    get_action_statuscode(&btn);
    /* strip modifier flags we don't care about */
    btn &= ~(BUTTON_REL | BUTTON_REPEAT);
    return btn == BUTTON_LEFT;
}

/* --- "Other Items" dynamic submenu ----------------------------------- */
/*
 * We can't reuse root_menu_ because its item_callback would hide the very
 * items we want to show.  Build a fresh menu_item_ex over the same
 * pointers from menu_table[] every time the user opens "Other Items".
 */

/* Flag flipped on while show_other_items() runs do_menu(), so the
 * shared item_callback below knows NOT to filter out hidden items from
 * the dynamic Other Items submenu — that's the one place they *should*
 * be visible. */
static bool g_showing_other_items = false;

static struct menu_item_ex *other_items_array[16]; /* > MAX_MENU_ITEMS */
static struct menu_item_ex  other_items_menu_;
static int other_items_callback(int action,
                                const struct menu_item_ex *this_item,
                                struct gui_synclist *this_list);
static struct menu_callback_with_desc other_items_desc = {
    other_items_callback, ID2P(LANG_OTHER_ITEMS), Icon_Submenu };

/* "Audiobooks" root-menu handler — opens the file browser rooted at
 * the user's configured audiobook_path.
 *
 * On hosted-Android targets (the Y1) the actual mount is /sdcard, so a
 * configured value of "/Audiobooks" resolves to a non-existent root
 * and the file browser falls all the way back to "/sdcard" — showing
 * Audiobooks, Music, Playlists, Themes, etc. side-by-side instead of
 * diving into Audiobooks.  Normalise to /sdcard<path> on Android when
 * the configured path doesn't already start with /sdcard. */
static int browse_audiobooks(void *param)
{
    (void)param;
    static char resolved[MAX_PATH];

    const char *configured = (const char *)global_settings.audiobook_path;
    if (!configured || !configured[0])
        configured = "/Audiobooks";

#if (CONFIG_PLATFORM & PLATFORM_ANDROID)
    /* If the user typed "/Audiobooks" (Rockbox-canonical), turn it into
     * "/sdcard/Audiobooks" (Android-actual).  If they already typed the
     * explicit /sdcard/... form, leave it alone. */
    if (configured[0] == '/' &&
        strncmp(configured, "/sdcard", 7) != 0)
    {
        snprintf(resolved, sizeof(resolved), "/sdcard%s", configured);
    }
    else
#endif
    {
        strmemccpy(resolved, configured, sizeof(resolved));
    }

    /* rockbox_browse → set_current_file_ex treats its root argument as a
     * FILE path: it strrchr()s the last '/' and uses the leading part
     * as currdir, the trailing part as the highlighted filename.  Without
     * a trailing slash, "/sdcard/Audiobooks" opens "/sdcard/" with the
     * "Audiobooks" folder highlighted — the user then has to tap it
     * again.  Append a slash so currdir IS the audiobook folder. */
    size_t rlen = strlen(resolved);
    if (rlen > 0 && rlen + 1 < sizeof(resolved) && resolved[rlen-1] != '/')
    {
        resolved[rlen]   = '/';
        resolved[rlen+1] = '\0';
    }

    struct browse_context browse = {
        .dirfilter = global_settings.dirfilter,
        .icon      = Icon_Bookmark,
        .root      = resolved,
    };
    return rockbox_browse(&browse);
}

static int show_other_items(void *param)
{
    (void)param;
    int count = 0;
    extern int MAX_MENU_ITEMS_count(void); /* not used */
    /* enumerate menu_table; pick out the hidden ones (and never
     * "other_items" itself, even if somehow tagged as hidden) */
    extern struct menu_table *root_menu_get_options(int *nb);
    int nb = 0;
    struct menu_table *table = root_menu_get_options(&nb);
    for (int i = 0; i < nb && count < (int)ARRAYLEN(other_items_array); i++)
    {
        if (table[i].item == &other_items)
            continue;
        if (main_menu_is_hidden(table[i].string))
            other_items_array[count++] = (struct menu_item_ex *)table[i].item;
    }
    if (count == 0)
    {
        splash(HZ, ID2P(LANG_BOOKMARK_LOAD_EMPTY)); /* reuse "empty" splash */
        return GO_TO_PREVIOUS;
    }

    other_items_menu_.flags = MENU_HAS_DESC | MT_MENU | MENU_ITEM_COUNT(count);
    other_items_menu_.submenus = (const struct menu_item_ex **)other_items_array;
    other_items_menu_.callback_and_desc = &other_items_desc;

    int selected = 0;
    g_showing_other_items = true;
    int ret = do_menu(&other_items_menu_, &selected, NULL, false);
    g_showing_other_items = false;
    /* do_menu returns either the value of the selected MT_RETURN_VALUE
     * (a GO_TO_*) or MENU_SELECTED_EXIT / MENU_ATTACHED_USB.  Pass it
     * back to root_menu's outer state machine. */
    return ret;
}

static int other_items_callback(int action,
                                const struct menu_item_ex *this_item,
                                struct gui_synclist *this_list)
{
    if (action == ACTION_STD_CANCEL
        && action_came_from_left_button() && this_list)
    {
        int sel = get_menu_selection(
            gui_synclist_get_sel_pos(this_list), this_item);
        const struct menu_item_ex *highlighted = NULL;
        if (this_item->submenus
            && sel >= 0
            && sel < (int)MENU_GET_COUNT(this_item->flags))
        {
            highlighted = this_item->submenus[sel];
        }
        if (highlighted)
        {
            const char *key = main_menu_key_for_item(highlighted);
            if (key && confirm_unhide_prompt())
            {
                main_menu_remove_hidden(key);
                /* Bail to the outer show_other_items() so it can rebuild
                 * the dynamic list — the item we just unhid needs to
                 * disappear from this view. */
                return ACTION_STD_CANCEL;
            }
            return ACTION_RELOAD_MENU;
        }
    }
    return action;
}

/* --- forward-declared earlier; defined here so menu_table[] is in scope */
static const char *main_menu_key_for_item(const struct menu_item_ex *item)
{
    int nb = 0;
    struct menu_table *table = root_menu_get_options(&nb);
    for (int i = 0; i < nb; i++)
        if (table[i].item == item)
            return table[i].string;
    return NULL;
}

/* All root-menu items use item_callback so the ACTION_REQUEST_MENUITEM
 * filter (which only fires on the *item's own* callback in menu.c) can
 * hide entries that the user has moved to "Other Items". */
MENUITEM_RETURNVALUE(shortcut_menu, ID2P(LANG_SHORTCUTS), GO_TO_SHORTCUTMENU,
                        item_callback, Icon_Bookmark);

MENUITEM_RETURNVALUE(file_browser, ID2P(LANG_DIR_BROWSER), GO_TO_FILEBROWSER,
                        item_callback, Icon_file_view_menu);
#ifdef HAVE_TAGCACHE
MENUITEM_RETURNVALUE(db_browser, ID2P(LANG_TAGCACHE), GO_TO_DBBROWSER,
                        item_callback, Icon_Audio);
#endif
MENUITEM_RETURNVALUE(rocks_browser, ID2P(LANG_PLUGINS), GO_TO_BROWSEPLUGINS,
                        item_callback, Icon_Plugin);

static char *get_wps_item_name(int selected_item, void * data,
                               char *buffer, size_t buffer_len)
{
    (void)selected_item; (void)data; (void)buffer; (void)buffer_len;
    if (audio_status())
        return ID2P(LANG_NOW_PLAYING);
    return ID2P(LANG_RESUME_PLAYBACK);
}
MENUITEM_RETURNVALUE_DYNTEXT(wps_item, GO_TO_WPS, item_callback, get_wps_item_name,
                                NULL, NULL, Icon_Playback_menu);
#ifdef HAVE_RECORDING
MENUITEM_RETURNVALUE(rec, ID2P(LANG_RECORDING), GO_TO_RECSCREEN,
                        item_callback, Icon_Recording);
#endif
#if CONFIG_TUNER
MENUITEM_RETURNVALUE(fm, ID2P(LANG_FM_RADIO), GO_TO_FM,
                        item_callback, Icon_Radio_screen);
#endif
MENUITEM_RETURNVALUE(menu_, ID2P(LANG_SETTINGS), GO_TO_MAINMENU,
                        item_callback, Icon_Submenu_Entered);
MENUITEM_RETURNVALUE(bookmarks, ID2P(LANG_BOOKMARK_MENU_RECENT_BOOKMARKS),
                        GO_TO_RECENTBMARKS,  item_callback,
                        Icon_Bookmark);
MENUITEM_RETURNVALUE(playlists, ID2P(LANG_PLAYLISTS), GO_TO_PLAYLISTS_SCREEN,
                     item_callback, Icon_Playlist);
MENUITEM_RETURNVALUE(system_menu_, ID2P(LANG_SYSTEM), GO_TO_SYSTEM_SCREEN,
                     item_callback, Icon_System_menu);
/* "Music" — alias for Database that auto-descends into the Artist
 * menu, so the user skips the tagtree root listing. */
MENUITEM_RETURNVALUE(music_item, ID2P(LANG_MUSIC), GO_TO_MUSIC,
                     item_callback, Icon_Audio);
/* "Audiobooks" — opens the file browser rooted at the configured
 * audiobook path so chapters stay in folder order rather than being
 * mangled into the DB's artist/album hierarchy. */
MENUITEM_RETURNVALUE(audiobooks_item, ID2P(LANG_AUDIOBOOKS_MENU),
                     GO_TO_AUDIOBOOKS_BROWSE,
                     item_callback, Icon_Bookmark);

struct menu_item_ex root_menu_;
static struct menu_callback_with_desc root_menu_desc = {
        item_callback, ID2P(LANG_ROCKBOX_TITLE), Icon_Rockbox };

static struct menu_table menu_table[] = {
    /* Order here represents the default ordering */
    { "bookmarks", &bookmarks },
    { "files", &file_browser },
#ifdef HAVE_TAGCACHE
    { "database", &db_browser },
#endif
    { "wps", &wps_item },
    { "settings", &menu_ },
#ifdef HAVE_RECORDING
    { "recording", &rec },
#endif
#if CONFIG_TUNER
    { "radio", &fm },
#endif
    { "playlists", &playlists },
#ifdef INNIOASIS_Y1
    { "fm_radio_app", &fm_radio_app_item },
#endif
    { "plugins", &rocks_browser },
    { "system_menu", &system_menu_ },
    { "shortcuts", &shortcut_menu },
    /* Y1 quality-of-life shortcuts: Music → DB browser, Audiobooks →
     * file browser at audiobook_path. */
    { "music", &music_item },
    { "audiobooks", &audiobooks_item },
    /* "Other Items" is always the last entry and is never user-hideable —
     * it's the escape hatch back to anything you've moved here. */
    { "other_items", &other_items },
};
#define MAX_MENU_ITEMS (sizeof(menu_table) / sizeof(struct menu_table))
static struct menu_item_ex *root_menu__[MAX_MENU_ITEMS];

struct menu_table *root_menu_get_options(int *nb_options)
{
    *nb_options = MAX_MENU_ITEMS;

    return menu_table;
}

void root_menu_load_from_cfg(void* setting, char *value)
{
    char *next = value, *start, *end;
    unsigned int menu_item_count = 0, i;
    bool main_menu_added = false;

    if (*value == '-')
    {
        root_menu_set_default(setting, NULL);
        return;
    }
    root_menu_.flags = MENU_HAS_DESC | MT_MENU;
    root_menu_.submenus = (const struct menu_item_ex **)&root_menu__;
    root_menu_.callback_and_desc = &root_menu_desc;

    while (next && menu_item_count < MAX_MENU_ITEMS)
    {
        start = next;
        next = strchr(next, ',');
        if (next)
        {
            *next = '\0';
            next++;
        }
        start = skip_whitespace(start);
        if ((end = strchr(start, ' ')))
            *end = '\0';
        for (i=0; i<MAX_MENU_ITEMS; i++)
        {
            if (*start && !strcmp(start, menu_table[i].string))
            {
                root_menu__[menu_item_count++] = (struct menu_item_ex *)menu_table[i].item;
                if (menu_table[i].item == &menu_)
                    main_menu_added = true;
                break;
            }
        }
    }
    if (!main_menu_added)
        root_menu__[menu_item_count++] = (struct menu_item_ex *)&menu_;

    /* Force-append entries that the user can't manage themselves yet
     * (we added them after this user's `root menu order:` was first
     * saved).  Without this, an existing config locks them out forever. */
    const struct menu_item_ex *forced[] = {
        &music_item,
        &audiobooks_item,
        &other_items,   /* must be last — see comment in menu_table[] */
    };
    for (size_t f = 0; f < sizeof(forced)/sizeof(forced[0]); f++)
    {
        bool already = false;
        for (i = 0; i < menu_item_count; i++)
            if (root_menu__[i] == forced[f]) { already = true; break; }
        if (!already && menu_item_count < MAX_MENU_ITEMS)
            root_menu__[menu_item_count++] = (struct menu_item_ex *)forced[f];
    }
    root_menu_.flags |= MENU_ITEM_COUNT(menu_item_count);
    *(bool*)setting = true;
}

char* root_menu_write_to_cfg(void* setting, char*buf, int buf_len)
{
    (void)setting;
    unsigned i, written, j;
    for (i = 0; i < MENU_GET_COUNT(root_menu_.flags); i++)
    {
        for (j=0; j<MAX_MENU_ITEMS; j++)
        {
            if (menu_table[j].item == root_menu__[i])
            {
                written = snprintf(buf, buf_len, "%s, ", menu_table[j].string);
                buf_len -= written;
                buf += written;
                break;
            }
        }
    }
    return buf;
}

void root_menu_set_default(void* setting, void* defaultval)
{
    unsigned i;
    (void)defaultval;

    root_menu_.flags = MENU_HAS_DESC | MT_MENU;
    root_menu_.submenus = (const struct menu_item_ex **)&root_menu__;
    root_menu_.callback_and_desc = &root_menu_desc;

    for (i=0; i<MAX_MENU_ITEMS; i++)
    {
        root_menu__[i] = (struct menu_item_ex *)menu_table[i].item;
    }
    root_menu_.flags |= MENU_ITEM_COUNT(MAX_MENU_ITEMS);
    *(bool*)setting = false;
}

bool root_menu_is_changed(void* setting, void* defaultval)
{
    (void)defaultval;
    return *(bool*)setting;
}

int item_callback(int action,
                  const struct menu_item_ex *this_item,
                  struct gui_synclist *this_list)
{
    (void)this_list;
    switch (action)
    {
        case ACTION_TREE_STOP:
            return ACTION_REDRAW;
        case ACTION_REQUEST_MENUITEM:
#if CONFIG_TUNER
            if (this_item == &fm)
            {
                if (radio_hardware_present() == 0)
                    return ACTION_EXIT_MENUITEM;
            }
            else
#endif
                if (this_item == &bookmarks)
            {
                if (global_settings.usemrb == 0)
                    return ACTION_EXIT_MENUITEM;
            }
            /* Filter user-hidden items, but ONLY when rendering the
             * root menu — inside the Other Items submenu these items
             * are exactly the ones we want to show.  "other_items"
             * itself is the escape hatch back to them and must never
             * disappear regardless of what's in main_menu_hidden. */
            if (!g_showing_other_items && this_item != &other_items)
            {
                const char *key = main_menu_key_for_item(this_item);
                if (key && main_menu_is_hidden(key))
                    return ACTION_EXIT_MENUITEM;
            }
        break;
        case ACTION_STD_CANCEL:
            /* On the root menu, the rewind button (BUTTON_LEFT) is the
             * only meaningful source of STD_CANCEL — there's nothing to
             * back out to from the top of the menu stack.  Hijack it to
             * stash the highlighted item in "Other Items".  Long-press
             * Play also produces STD_CANCEL on the Y1, so we check the
             * literal triggering button to avoid an accidental hide.
             *
             * For navigation actions menu.c passes the *parent* menu as
             * this_item (not the highlighted child), so we have to fish
             * the highlighted submenu out of the synclist ourselves. */
            if (action_came_from_left_button() && this_list)
            {
                int sel = get_menu_selection(
                    gui_synclist_get_sel_pos(this_list), this_item);
                const struct menu_item_ex *highlighted = NULL;
                if (this_item->submenus
                    && sel >= 0
                    && sel < (int)MENU_GET_COUNT(this_item->flags))
                {
                    highlighted = this_item->submenus[sel];
                }
                if (highlighted && highlighted != &other_items)
                {
                    const char *key = main_menu_key_for_item(highlighted);
                    if (key && !main_menu_is_hidden(key)
                        && confirm_hide_prompt())
                    {
                        main_menu_add_hidden(key);
                        /* RELOAD_MENU re-runs init_menu_lists so the
                         * REQUEST_MENUITEM filter sweeps out the now-
                         * hidden entry. */
                        return ACTION_RELOAD_MENU;
                    }
                    return ACTION_REDRAW;
                }
            }
            break;
    }
    return action;
}

static int get_selection(int last_screen)
{
    int i;
    int len = ARRAYLEN(root_menu__);
    for(i=0; i < len; i++)
    {
        if (((root_menu__[i]->flags&MENU_TYPE_MASK) == MT_RETURN_VALUE) &&
            (root_menu__[i]->value == last_screen))
        {
            return i;
        }
    }
    return 0;
}

static inline int load_screen(int screen)
{
    /* set the global_status.last_screen before entering,
        if we dont we will always return to the wrong screen on boot */
    int old_previous = last_screen;
    int ret_val;
    enum current_activity activity = ACTIVITY_UNKNOWN;
    if (screen <= GO_TO_ROOT)
        return screen;
    if (screen == old_previous)
        old_previous = GO_TO_ROOT;
    global_status.last_screen = (char)screen;
    status_save(false);

    if (screen == GO_TO_BROWSEPLUGINS)
        activity = ACTIVITY_PLUGINBROWSER;
    else if (screen == GO_TO_MAINMENU)
        activity = ACTIVITY_SETTINGS;
    else if (screen == GO_TO_SYSTEM_SCREEN)
        activity =  ACTIVITY_SYSTEMSCREEN;

    if (activity != ACTIVITY_UNKNOWN)
        push_current_activity(activity);

    ret_val = items[screen].function(items[screen].param);

    if (activity != ACTIVITY_UNKNOWN)
    {
        if (ret_val == GO_TO_PLUGIN
            || ret_val == GO_TO_WPS
            || ret_val == GO_TO_PREVIOUS_MUSIC
            || ret_val == GO_TO_PREVIOUS_BROWSER
            || ret_val == GO_TO_FILEBROWSER)
        {
            pop_current_activity_without_refresh();
        }
        else
            pop_current_activity();
    }

    last_screen = screen;
    if (ret_val == GO_TO_PREVIOUS)
        last_screen = old_previous;
    return ret_val;
}

static int load_context_screen(int selection)
{
    const struct menu_item_ex *context_menu = NULL;
    int retval = GO_TO_PREVIOUS;
    push_current_activity(ACTIVITY_CONTEXTMENU);
    if ((root_menu__[selection]->flags&MENU_TYPE_MASK) == MT_RETURN_VALUE)
    {
        int item = root_menu__[selection]->value;
        context_menu = items[item].context_menu;
    }
    /* special cases */
    else if (root_menu__[selection] == &info_menu)
    {
        context_menu = &system_menu;
    }

    if (context_menu)
        retval = do_menu(context_menu, NULL, NULL, false);
    pop_current_activity();
    return retval;
}

static int load_plugin_screen(char *key)
{
    int ret_val = PLUGIN_ERROR;
    int loops = 100;
    int old_previous = last_screen;
    int old_global = global_status.last_screen;
    last_screen = next_screen;
    global_status.last_screen = (char)next_screen;

    while(loops-- > 0) /* just to keep things from getting out of hand */
    {
        int opret = open_plugin_load_entry(key);
        struct open_plugin_entry_t *op_entry = open_plugin_get_entry();
        char *path = op_entry->path;
        char *param = op_entry->param;
        if (param[0] == '\0')
            param = NULL;
        if (path[0] == '\0' && key)
            path = P2STR((unsigned char *)key);
        int ret = plugin_load(path, param);

        if (ret == PLUGIN_USB_CONNECTED || ret == PLUGIN_ERROR)
            ret_val = GO_TO_ROOT;
        else if (ret == PLUGIN_GOTO_WPS)
            ret_val = GO_TO_WPS;
        else if (ret == PLUGIN_GOTO_PLUGIN)
        {
            if(op_entry->lang_id == LANG_OPEN_PLUGIN)
            {
                if (key == (char*)ID2P(LANG_SHORTCUTS))
                {
                    op_entry->lang_id = LANG_SHORTCUTS;
                }
                else /* Bugfix ensure proper key */
                {
                    key = ID2P(LANG_OPEN_PLUGIN);
                }
            }
            continue;
        }
        else
        {
            if (ret == PLUGIN_GOTO_ROOT)
                ret_val = GO_TO_ROOT;
            else
                ret_val = GO_TO_PREVIOUS;
            /* Prevents infinite loop with WPS, Plugins, Previous Screen*/
            if (ret == PLUGIN_OK && old_global == GO_TO_WPS && !audio_status())
                ret_val = GO_TO_ROOT;
            last_screen = (old_previous == next_screen || old_global == GO_TO_ROOT)
                ? GO_TO_ROOT : old_previous;
            if (last_screen == GO_TO_ROOT)
                global_status.last_screen = GO_TO_ROOT;
        }
        /* ret_val != GO_TO_PLUGIN */

        if (opret != OPEN_PLUGIN_NEEDS_FLUSHED || last_screen != GO_TO_WPS)
        {
            /* Keep the entry in case of GO_TO_PREVIOUS */
            op_entry->hash = 0; /*remove hash -- prevents flush to disk */
            op_entry->lang_id = LANG_PREVIOUS_SCREEN;
            /*open_plugin_add_path(NULL, NULL, NULL);// clear entry */
        }
        break;
    } /*while */
    return ret_val;
}

static void ignore_back_button_stub(bool ignore)
{
#if (defined(PLATFORM_ANDROID) || defined(INNIOASIS_Y1))
    /* BACK button to be handled by Android instead of rockbox */
    android_ignore_back_button(ignore);
#else
    (void) ignore;
#endif
}

static int root_menu_setup_screens(void)
{
    int new_screen = next_screen;
    if (global_settings.start_in_screen == 0)
        new_screen = global_status.last_screen;
    else
        new_screen = global_settings.start_in_screen - 2;

    if (new_screen >= NUM_ITEMS)
        new_screen = GO_TO_ROOT;
    else if (new_screen == GO_TO_PLUGIN)
    {
        if (global_status.last_screen == GO_TO_SHORTCUTMENU)
        {
            /* Can make this any value other than GO_TO_SHORTCUTMENU
               otherwise it takes over on startup when the user wanted
               the plugin at key - LANG_START_SCREEN */
            global_status.last_screen = GO_TO_PLUGIN;
        }
        if(global_status.last_screen == GO_TO_SHORTCUTMENU ||
           global_status.last_screen == GO_TO_PLUGIN)
        {
            if (global_settings.start_in_screen == 0)
            {  /* Start in: Previous Screen */
                last_screen = GO_TO_PREVIOUS;
                global_status.last_screen = GO_TO_ROOT;
                /* since the plugin has GO_TO_PLUGIN as origin it
                   will just return GO_TO_PREVIOUS <=> GO_TO_PLUGIN in a loop
                   To allow exit after restart we check for GO_TO_ROOT
                   if so exit to ROOT after the plugin exits */
            }
        }
    }
#if CONFIG_TUNER
    add_event(PLAYBACK_EVENT_START_PLAYBACK, rootmenu_start_playback_callback);
#endif
    add_event(PLAYBACK_EVENT_TRACK_CHANGE, rootmenu_track_changed_callback);
#ifdef HAVE_RTC_ALARM
    int alarm_wake_up_screen = 0;
    if ( rtc_check_alarm_started(true) )
    {
        rtc_enable_alarm(false);

#if (defined(HAVE_RECORDING) || CONFIG_TUNER)
        alarm_wake_up_screen = global_settings.alarm_wake_up_screen;
#endif
        switch (alarm_wake_up_screen)
        {
#if CONFIG_TUNER
            case ALARM_START_FM:
                new_screen = GO_TO_FM;
                break;
#endif
#ifdef HAVE_RECORDING
            case ALARM_START_REC:
                recording_start_automatic = true;
                new_screen = GO_TO_RECSCREEN;
                break;
#endif
            default:
                new_screen = GO_TO_WPS;
                break;
        } /* switch() */
    }
#endif /* HAVE_RTC_ALARM */

#if defined(HAVE_HEADPHONE_DETECTION) || defined(HAVE_LINEOUT_DETECTION)
    if (new_screen == GO_TO_WPS && global_settings.unplug_autoresume)
    {
       new_screen = GO_TO_ROOT;
#ifdef HAVE_HEADPHONE_DETECTION
        if (headphones_inserted())
            new_screen = GO_TO_WPS;
#endif
#ifdef HAVE_LINEOUT_DETECTION
        if (lineout_inserted())
            new_screen = GO_TO_WPS;
#endif
    }
#endif /*(HAVE_HEADPHONE_DETECTION) || (HAVE_LINEOUT_DETECTION)*/
    return new_screen;
}


void root_menu(void)
{
    int previous_browser = global_status.last_browser;
    int selected = 0;
    int shortcut_origin = GO_TO_ROOT;

    push_current_activity(ACTIVITY_MAINMENU);
    next_screen = root_menu_setup_screens();

    while (true)
    {
        switch (next_screen)
        {
            case MENU_ATTACHED_USB:
            case MENU_SELECTED_EXIT:
                /* fall through */
            case GO_TO_ROOT:
                if (last_screen != GO_TO_ROOT)
                    selected = get_selection(last_screen);
                global_status.last_screen = GO_TO_ROOT; /* We've returned to ROOT */
                /* When we are in the main menu we want the hardware BACK
                 * button to be handled by HOST instead of rockbox */
                ignore_back_button_stub(true);

                next_screen = do_menu(&root_menu_, &selected, NULL, false);

                ignore_back_button_stub(false);

                if (next_screen != GO_TO_PREVIOUS)
                    last_screen = GO_TO_ROOT;
                break;
#ifdef HAVE_TAGCACHE
            case GO_TO_DBBROWSER:
#endif
            case GO_TO_FILEBROWSER:
            case GO_TO_PLAYLISTS_SCREEN:
                global_status.last_browser = previous_browser = next_screen;
                goto load_next_screen;
                break;
#if CONFIG_TUNER
            case GO_TO_WPS:
            case GO_TO_FM:
                previous_music = next_screen;
                goto load_next_screen;
                break;
#endif /* With !CONFIG_TUNER previous_music is always GO_TO_WPS */

            case GO_TO_PREVIOUS:
            {
                next_screen = last_screen;
                if (last_screen == GO_TO_PLUGIN)/* for WPS */
                    last_screen = GO_TO_PREVIOUS;
                else if (last_screen == GO_TO_PREVIOUS)
                    next_screen = GO_TO_ROOT;
                break;
            }

            case GO_TO_PREVIOUS_BROWSER:
                next_screen = previous_browser;
                break;

            case GO_TO_PREVIOUS_MUSIC:
                next_screen = previous_music;
                break;
            case GO_TO_ROOTITEM_CONTEXT:
                next_screen = load_context_screen(selected);
                break;
            case GO_TO_PLUGIN:
            {

                char *key;
                if (global_status.last_screen == GO_TO_SHORTCUTMENU)
                {
                    struct open_plugin_entry_t *op_entry = open_plugin_get_entry();
                    if (op_entry->lang_id == LANG_OPEN_PLUGIN)
                        op_entry->lang_id = LANG_SHORTCUTS;
                    shortcut_origin = last_screen;
                    key = ID2P(LANG_SHORTCUTS);
                }
                else
                {
                    switch (last_screen)
                    {
                        case GO_TO_ROOT:
                            key = ID2P(LANG_START_SCREEN);
                            break;
                        case GO_TO_WPS:
                            key = ID2P(LANG_OPEN_PLUGIN_SET_WPS_CONTEXT_PLUGIN);
                            break;
                        case GO_TO_SHORTCUTMENU:
                            key = ID2P(LANG_SHORTCUTS);
                            break;
                        case GO_TO_PREVIOUS:
                            key = ID2P(LANG_PREVIOUS_SCREEN);
                            break;
                        default:
                            key = ID2P(LANG_OPEN_PLUGIN);
                            break;
                    }
                }


                push_activity_without_refresh(ACTIVITY_UNKNOWN); /* prevent plugin_load */
                next_screen = load_plugin_screen(key);           /* from flashing root  */
                pop_current_activity_without_refresh();          /* menu activity       */

                if (next_screen == GO_TO_PREVIOUS)
                {
                    /* shortcuts may take several trips through the GO_TO_PLUGIN
                       case make sure we preserve and restore the origin */
                    if(tree_get_context()->out_of_tree > 0) /* a shortcut has been selected */
                    {
                        next_screen = GO_TO_FILEBROWSER;
                        shortcut_origin = GO_TO_ROOT;
                        /* note in some cases there is a screen to return to
                        but the history is rewritten as if you browsed here
                        from the root so return there when finished */
                    }
                    else if (shortcut_origin != GO_TO_ROOT)
                    {
                        if (shortcut_origin != GO_TO_WPS)
                            next_screen = shortcut_origin;
                        shortcut_origin = GO_TO_ROOT;
                    }
                    /* skip GO_TO_PREVIOUS */
                    if (last_screen == GO_TO_BROWSEPLUGINS)
                    {
                        next_screen = last_screen;
                        last_screen = GO_TO_PLUGIN;
                    }
                }
                previous_browser = (next_screen == GO_TO_WPS) ?
                                   GO_TO_PLUGIN : global_status.last_browser;
                break;
            }
            default:
                goto load_next_screen;
                break;
        } /* switch() */
        continue;
load_next_screen: /* load_screen is inlined */
        next_screen = load_screen(next_screen);
    }

}

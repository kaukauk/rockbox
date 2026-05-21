#include "audiobook.h"
#include "settings.h"
#include "audio.h"
#include <string.h>
#include <ctype.h>

/* --------------------------------------------------------------------- */
/* Detection                                                             */
/* --------------------------------------------------------------------- */

static bool path_contains_segment(const char *path, const char *needle)
{
    if (!needle || !needle[0])
        return false;
    /* Skip a leading slash on the needle so "/Audiobooks" and "Audiobooks"
     * both work, then look for the needle anywhere in the path provided
     * it sits between path-separator boundaries (or end-of-string). This
     * way a user-configured "/Audiobooks" matches the hosted-Android path
     * "/sdcard/Audiobooks/foo.mp3" as well as the legacy "/Audiobooks/..".
     */
    while (*needle == '/')
        needle++;
    if (!*needle)
        return false;
    size_t nlen = strlen(needle);

    for (const char *p = path; *p; p++)
    {
        /* Each candidate must begin right after a '/' (or be at index 0). */
        if (p != path && p[-1] != '/')
            continue;
        if (strncasecmp(p, needle, nlen) != 0)
            continue;
        char term = p[nlen];
        if (term == '\0' || term == '/')
            return true;
    }
    return false;
}

static bool extension_in_list(const char *path, const char *list)
{
    if (!list || !list[0])
        return false;
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1])
        return false;
    const char *ext = dot + 1;
    size_t elen = strlen(ext);

    /* Walk a comma/space/semicolon-separated list of bare extensions. */
    const char *p = list;
    while (*p)
    {
        while (*p == ',' || *p == ' ' || *p == ';' || *p == '.')
            p++;
        const char *start = p;
        while (*p && *p != ',' && *p != ' ' && *p != ';')
            p++;
        size_t len = p - start;
        if (len == elen && strncasecmp(start, ext, elen) == 0)
            return true;
    }
    return false;
}

bool audiobook_is_active(void)
{
    if (!global_settings.audiobook_mode)
        return false;

    struct mp3entry *id3 = audio_current_track();
    if (!id3 || !id3->path[0])
        return false;

    return path_contains_segment(id3->path,
                                 (const char*)global_settings.audiobook_path)
        || extension_in_list(id3->path,
                             (const char*)global_settings.audiobook_extensions);
}

/* --------------------------------------------------------------------- */
/* Resolved-value getters                                                */
/* --------------------------------------------------------------------- */

int audiobook_skip_length(void)
{
    return audiobook_is_active() ? global_settings.audiobook_skip_length
                                 : global_settings.skip_length;
}

int audiobook_pause_rewind(void)
{
    return audiobook_is_active() ? global_settings.audiobook_pause_rewind
                                 : global_settings.pause_rewind;
}

int audiobook_autocreatebookmark(void)
{
    return audiobook_is_active() ? global_settings.audiobook_autocreatebookmark
                                 : global_settings.autocreatebookmark;
}

int audiobook_autoloadbookmark(void)
{
    return audiobook_is_active() ? global_settings.audiobook_autoloadbookmark
                                 : global_settings.autoloadbookmark;
}

int audiobook_usemrb(void)
{
    return audiobook_is_active() ? global_settings.audiobook_usemrb
                                 : global_settings.usemrb;
}

bool audiobook_autoresume_enable(void)
{
    return audiobook_is_active() ? global_settings.audiobook_autoresume_enable
                                 : global_settings.autoresume_enable;
}

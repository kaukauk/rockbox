/* audiobook.h — per-track-context settings overrides.
 *
 * Most users want one set of playback behaviours for music and a *different*
 * set for audiobooks (auto-bookmark, longer skip-length, resume on replay).
 * Rockbox's stock playback settings are global, so we shadow six of them
 * with a parallel "audiobook" value plus a path/extension matcher that
 * decides which value to apply for the currently-playing track.
 *
 *   global_settings.audiobook_mode == false  → always use the regular value
 *   global_settings.audiobook_mode == true   → use the audiobook value when
 *                                              the current track's path
 *                                              matches the configured
 *                                              prefix OR its extension
 *                                              matches the configured list
 */

#ifndef __AUDIOBOOK_H__
#define __AUDIOBOOK_H__

#include <stdbool.h>

/* True iff audiobook overrides should apply to the current track right now. */
bool audiobook_is_active(void);

/* True iff the given file path matches the audiobook patterns. Use this
 * variant when you are deciding on behavior for a track that is *about to
 * load* — at that moment audio_current_track() still points at the
 * outgoing track, so audiobook_is_active() would test the wrong thing. */
bool audiobook_path_matches(const char *path);

/* Resolved values keyed off audiobook_is_active() — for code that reacts
 * to the currently-playing track (skip, pause-rewind, autobookmark on
 * stop). */
int  audiobook_skip_length(void);          /* seconds */
int  audiobook_pause_rewind(void);         /* seconds */
int  audiobook_autocreatebookmark(void);   /* BOOKMARK_NO/YES/ASK/... */
int  audiobook_usemrb(void);               /* BOOKMARK_NO/YES/ONE_PER_* */

/* Resolved values keyed off a caller-supplied path — for code that
 * applies the setting to an *incoming* track or playlist (autoload on
 * playlist start, autoresume on track load). */
int  audiobook_autoloadbookmark_for(const char *path);
bool audiobook_autoresume_enable_for(const char *path);

#endif /* __AUDIOBOOK_H__ */

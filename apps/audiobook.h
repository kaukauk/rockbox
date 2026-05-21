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

/* Resolved values: returns the audiobook field if audiobook_is_active(),
 * otherwise the corresponding regular global_settings field. */
int  audiobook_skip_length(void);          /* seconds */
int  audiobook_pause_rewind(void);         /* seconds */
int  audiobook_autocreatebookmark(void);   /* BOOKMARK_NO/YES/ASK/... */
int  audiobook_autoloadbookmark(void);     /* BOOKMARK_NO/YES/ASK */
int  audiobook_usemrb(void);               /* BOOKMARK_NO/YES/ONE_PER_* */
bool audiobook_autoresume_enable(void);

#endif /* __AUDIOBOOK_H__ */

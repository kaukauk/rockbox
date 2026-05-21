/* music_browser.h — three-level tagcache-backed music browser.
 *
 * The stock Database (tagtree) menu surfaces a pile of meta entries the
 * Y1 user doesn't want — "<All tracks>", "<Random>", "<All tracks
 * sorted by album>", Album-Artist vs. Artist, search/runtime/etc.
 * music_browse() is a stripped-down replacement: artist list → album
 * list → track list → play.  Nothing else.  Reuses the tagcache that
 * Database is already maintaining; just doesn't go through tagtree.c.
 */

#ifndef __MUSIC_BROWSER_H__
#define __MUSIC_BROWSER_H__

/* Root-menu handler signature: int (*)(void *param).  Returns one of
 * the GO_TO_* values from root_menu.h.  GO_TO_PREVIOUS on user cancel,
 * GO_TO_WPS when a track has been queued and playback started. */
int music_browse(void *param);

#endif /* __MUSIC_BROWSER_H__ */

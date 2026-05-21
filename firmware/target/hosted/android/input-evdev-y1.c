/*
 * Innioasis Y1 — direct evdev reader for screen-off button delivery.
 *
 * The Y1's playback controls (rewind/FF/pause/stop and the volume scroll
 * wheel) report through the touch-panel chip as evdev key events on
 * /dev/input/event2 ("mtk-tpd").  None of those keycodes carry the WAKE
 * flag in the device's keylayout, so when the screen is off Android's
 * InputDispatcher drops them before the app ever sees them — leaving
 * the user unable to skip, pause, or change volume without first waking
 * the screen.
 *
 * This file bypasses Android's input pipeline entirely by reading raw
 * input_event structs from /dev/input/event2 directly.  It's the same
 * approach Android's own InputReader uses internally; we just run a
 * private copy of the relevant slice for the buttons we care about and
 * post the resulting events straight into the Rockbox button queue.
 *
 * Gating: events are forwarded only when the screen is logically off
 * AND audio is actively playing.  Otherwise the read events are
 * discarded so the normal Android → Java → Rockbox path stays
 * authoritative.  The middle button (KEY_REPLY on event0) is
 * intentionally not handled here — it carries WAKE in the keylayout
 * and keeps its existing wake-the-screen behaviour through Android.
 *
 * Permission note: /dev/input/event* is owned root:input on the Y1.
 * Our APK runs with sharedUserId="android.uid.system" but the system
 * uid is not automatically a member of group `input` on Android 4.2.x,
 * so the open() will fail with EACCES on a stock ROM.  rb-install.sh
 * runs `chmod 0666 /dev/input/event*` via the device's su binary as a
 * runtime fix; a persistent fix would belong in init.rc.
 */

#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <android/log.h>
#include <jni.h>

#include "button.h"
#include "audio.h"

#define LOG_TAG "input-evdev-y1"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#ifdef INNIOASIS_Y1

/* true while the screen is logically off (Android PowerManager).  Set
 * by Java via Java_org_rockbox_RockboxService_notifyScreenState. */
static volatile bool screen_is_off = false;

static int linux_keycode_to_button(int keycode)
{
    switch (keycode)
    {
        case KEY_LEFT:      return BUTTON_LEFT;        /* rewind        */
        case KEY_RIGHT:     return BUTTON_RIGHT;       /* fast-forward  */
        case KEY_PLAYPAUSE: return BUTTON_PLAY;        /* pause/stop    */
        case KEY_DOWN:      return BUTTON_SCROLL_FWD;  /* scroll CW  -> vol up   */
        case KEY_UP:        return BUTTON_SCROLL_BACK; /* scroll ACW -> vol down */
        default:            return 0;
    }
}

static int find_keypad_device(void)
{
    for (int i = 0; i < 16; i++)
    {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        char name[80] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
            strcmp(name, "mtk-tpd") == 0)
        {
            LOGI("using %s (%s)", path, name);
            return fd;
        }
        close(fd);
    }
    LOGI("no mtk-tpd device found in /dev/input — events will not be delivered");
    return -1;
}

static void *evdev_thread(void *arg)
{
    (void)arg;

    int fd = find_keypad_device();
    if (fd < 0) return NULL;

    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    struct input_event ev;

    while (1)
    {
        int rc = poll(&pfd, 1, -1);
        if (rc < 0)
        {
            if (errno == EINTR) continue;
            LOGI("poll failed: %s", strerror(errno));
            break;
        }

        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n != sizeof(ev))
        {
            if (n < 0 && errno == EINTR) continue;
            continue;
        }

        if (ev.type != EV_KEY) continue;

        int button = linux_keycode_to_button(ev.code);
        if (!button) continue;

        bool playing = (audio_status() & AUDIO_STATUS_PLAY) != 0;
        if (!(screen_is_off && playing)) continue;

        if (ev.value == 1)
            button_queue_post(button, 0);
        else if (ev.value == 0)
            button_queue_post(button | BUTTON_REL, 0);
        /* ev.value == 2 (kernel autorepeat) is dropped — the action
         * engine generates its own repeat events. */
    }

    close(fd);
    return NULL;
}

void input_evdev_init(void)
{
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, evdev_thread, NULL) != 0)
        LOGI("pthread_create failed: %s", strerror(errno));
    pthread_attr_destroy(&attr);
}

JNIEXPORT void JNICALL
Java_org_rockbox_RockboxService_notifyScreenState(JNIEnv *env, jclass cls,
                                                  jboolean is_on)
{
    (void)env;
    (void)cls;
    screen_is_off = !is_on;
}

#endif /* INNIOASIS_Y1 */

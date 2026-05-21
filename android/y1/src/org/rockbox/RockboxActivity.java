/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2010 Thomas Martitz
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

package org.rockbox;

import org.rockbox.Helper.MediaButtonReceiver;
import android.app.Activity;
import android.app.ProgressDialog;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.ResultReceiver;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Toast;
import android.content.Context;
import android.os.PowerManager;
import android.util.Log;
import android.app.ActivityManager;
import java.util.List;

public class RockboxActivity extends Activity 
{
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) 
    {
        System.loadLibrary("rockbox");
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                             WindowManager.LayoutParams.FLAG_FULLSCREEN);
        Intent intent = new Intent(this, RockboxService.class);
        intent.setAction(Intent.ACTION_MAIN);
        intent.putExtra("callback", new ResultReceiver(new Handler(getMainLooper())) {
            private boolean unzip = false;
            private ProgressDialog loadingdialog;
            private void createProgressDialog()
            {
                loadingdialog = new ProgressDialog(RockboxActivity.this);
                loadingdialog.setMessage(getString(R.string.rockbox_extracting));
                loadingdialog.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
                loadingdialog.setIndeterminate(true);
                loadingdialog.setCancelable(false);
                loadingdialog.show();
            }

            @Override
            protected void onReceiveResult(final int resultCode, final Bundle resultData)
            {
                RockboxFramebuffer fb;
                switch (resultCode) {
                    case RockboxService.RESULT_INVOKING_MAIN:
                        if (loadingdialog != null)
                            loadingdialog.dismiss();
                        fb = new RockboxFramebuffer(RockboxActivity.this);
                        setContentView(fb);
                        fb.requestFocus();
                        break;
                    case RockboxService.RESULT_LIB_LOAD_PROGRESS:
                        if (loadingdialog == null)
                            createProgressDialog();
                        loadingdialog.setIndeterminate(false);
                        loadingdialog.setMax(resultData.getInt("max", 100));
                        loadingdialog.setProgress(resultData.getInt("value", 0));
                        break;
                    case RockboxService.RESULT_LIB_LOADED:
                        unzip = resultData.getBoolean("unzip");
                        break;
                    case RockboxService.RESULT_SERVICE_RUNNING:
                        if (!unzip) /* defer to RESULT_INVOKING_MAIN */
                        {
                            fb = new RockboxFramebuffer(RockboxActivity.this);
                            setContentView(fb);
                            fb.requestFocus();
                        }
                        setServiceActivity(true);
                        break;
                    case RockboxService.RESULT_ERROR_OCCURED:
                        Toast.makeText(RockboxActivity.this, resultData.getString("error"), Toast.LENGTH_LONG);
                        break;
                    case RockboxService.RESULT_ROCKBOX_EXIT:
                        finish();
                        break;
                }
            }
        });
        startService(intent);
    }

    private void setServiceActivity(boolean set)
    {
        RockboxService s = RockboxService.getInstance();
        if (s != null)
            s.setActivity(set ? this : null);
    }

    public void onResume()
    {
        super.onResume();
        RockboxFramebuffer fb = new RockboxFramebuffer(this);
        setContentView(fb);
        fb.requestFocus();
        setVisible(true);
        setServiceActivity(true);
        /* disable key lock */
        keyLock(false);
    }
    
    /* this is also called when the backlight goes off,
     * which is nice
     */
    @Override
    protected void onPause()
    {
        super.onPause();
        /* this will cause the framebuffer's Surface to be destroyed, enabling
         * us to disable drawing */
        setVisible(false);
        /* Do NOT enable the softlock here.  The screen turning off must
         * not silently disable the playback controls — the direct evdev
         * reader in firmware/target/hosted/android/input-evdev-y1.c
         * delivers rewind/FF/pause/scroll into the button queue while
         * the screen is off + audio is playing, and a softlock would
         * filter every action out in do_softlock(). */
    }
    
    @Override
    protected void onStop() 
    {
        super.onStop();
        setServiceActivity(false);
    }
    
    @Override
    protected void onDestroy() 
    {
        super.onDestroy();
        setServiceActivity(false);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        boolean screenOn = pm.isScreenOn();
        if (hasFocus) {
            MediaButtonReceiver.setDpadMode(0);
            Log.d("RockboxActivity", "back in Rockbox, disable dpad mode");
        }
        /* also check if the screen is on since the activity also loose focus if screen is off while in Rockbox */
        else if (!hasFocus && screenOn) {
            String foregroundPackage = getForegroundPackageName(this);
            if (foregroundPackage.equals("com.mediatek.FMRadio")){
                MediaButtonReceiver.setDpadMode(1); // fm specific remapping
            } else if (isSystemPackage(foregroundPackage)) {
                /* Focus got stolen by a transient system overlay — the
                 * USB storage mode dialog when a cable is plugged in,
                 * the volume HUD, the lock screen, a toast.  The Y1's
                 * scroll wheel events go to whatever window is focused,
                 * and those overlays don't react to wheel input, so the
                 * user perceives Rockbox as frozen.  Pull our activity
                 * back to the top to reclaim focus.  Keep dpad_mode at
                 * 0 either way so any presses landing through media-
                 * button broadcasts during the swap still reach our C
                 * code. */
                MediaButtonReceiver.setDpadMode(0);
                Log.d("RockboxActivity",
                      "system overlay (" + foregroundPackage
                      + ") stole focus — reclaiming");
                Intent reclaim = new Intent(this, RockboxActivity.class);
                reclaim.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                               | Intent.FLAG_ACTIVITY_SINGLE_TOP);
                startActivity(reclaim);
            } else {
                MediaButtonReceiver.setDpadMode(2); // other menus
            }
        }
    }

    /* True for packages that we never want to treat as "the user
     * actually switched to a different media app".  Catches the USB
     * dialog (com.android.systemui), the launcher, the Android-internal
     * permission/UI activities, and the catch-all "unknown" fallback. */
    private boolean isSystemPackage(String pkg) {
        if (pkg == null)
            return true;
        return pkg.startsWith("com.android.")
            || pkg.startsWith("android")
            || pkg.equals("unknown");
    }

    private String getForegroundPackageName(Context context) {
        ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);

        try {
            List<ActivityManager.RunningTaskInfo> tasks = am.getRunningTasks(1);
            if (tasks != null && !tasks.isEmpty()) {
                return tasks.get(0).baseActivity.getPackageName();
            }
        } catch (Exception e) {
            Log.e("RockboxActivity", "Error getting foreground package", e);
        }

        return "unknown";
    }

    /* call native method to trigger key lock */
    public native static void keyLock(boolean keyLock);

}

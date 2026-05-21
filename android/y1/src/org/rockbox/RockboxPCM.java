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

import java.util.Arrays;
import org.rockbox.Helper.Logger;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Process;

public class RockboxPCM extends AudioTrack 
{
    private static final int streamtype = AudioManager.STREAM_MUSIC;
    private static final int samplerate = 44100;
    /* should be CHANNEL_OUT_STEREO in 2.0 and above */
    private static final int channels = 
            AudioFormat.CHANNEL_OUT_STEREO;
    private static final int encoding = 
            AudioFormat.ENCODING_PCM_16BIT;
    private AudioManager audiomanager;
    private RockboxService rbservice;
    private byte[] raw_data;

    private int refillmark;
    private int maxstreamvolume;
    private int setstreamvolume = -1;
    private float minpcmvolume;
    private float curpcmvolume = 0;
    private float curleftvol = 1f;
    private float currightvol = 1f;
    private float pcmrange;

    /* 8k is plenty, but some devices may have a higher minimum.
     * 8k represents 125ms of audio */
    private static final int chunkSize =
        Math.max(8<<10, getMinBufferSize(samplerate, channels, encoding));
    Streamer streamer;

    public RockboxPCM()
    {
        super(streamtype, samplerate,  channels, encoding,
                chunkSize, AudioTrack.MODE_STREAM);

        streamer = new Streamer(chunkSize);
        streamer.start();
        raw_data = new byte[chunkSize]; /* in shorts */
        Arrays.fill(raw_data, (byte) 0);

        /* find cleaner way to get context? */
        rbservice = RockboxService.getInstance();
        audiomanager =
            (AudioManager) rbservice.getSystemService(Context.AUDIO_SERVICE);
        maxstreamvolume = audiomanager.getStreamMaxVolume(streamtype);

        minpcmvolume = getMinVolume();
        pcmrange = getMaxVolume() - minpcmvolume;

        setupVolumeHandler();
        postVolume(audiomanager.getStreamVolume(streamtype));
    }

    /**
     * This class does the actual playback work. Its run() method
     * continuously writes data to the AudioTrack. This operation blocks
     * and should therefore be run on its own thread.
     */
    private class Streamer extends Thread
    {
        byte[] buffer;
        private boolean quit = false;

        Streamer(int bufsize)
        {
            super("audio thread");
            buffer = new byte[bufsize];
        }

        @Override
        public void run()
        {
            /* THREAD_PRIORITY_URGENT_AUDIO can only be specified via
             * setThreadPriority(), and not via thread.setPriority(). This is
             * also how the android's HandlerThread class implements it */
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
            while (!quit)
            {
                switch(getPlayState())
                {
                    case PLAYSTATE_PLAYING:
                        nativeWrite(buffer, buffer.length);
                        break;
                    case PLAYSTATE_PAUSED:
                    case PLAYSTATE_STOPPED:
                    {
                        synchronized (this)
                        {
                            try
                            {
                                wait();
                            }
                            catch (InterruptedException e) { e.printStackTrace(); }
                            break;
                        }
                    }
                }
            }
        }

        synchronized void quit()
        {
            quit = true;
            notify();
        }

        synchronized void kick()
        {
            notify();
        }

        void quitAndJoin()
        {
            while(true)
            {
                try
                {
                    quit();
                    join();
                    return;
                }
                catch (InterruptedException e) { }
            }
        }
    }

    private native void postVolumeChangedEvent(int volume);

    private void postVolume(int volume)
    {
        int rbvolume = ((maxstreamvolume - volume) * -99) /
            maxstreamvolume;
        Logger.d("java:postVolumeChangedEvent, avol "+volume+" rbvol "+rbvolume);
        postVolumeChangedEvent(rbvolume);
    }
    
    private void setupVolumeHandler()
    {
        BroadcastReceiver broadcastReceiver = new BroadcastReceiver()
        {
            @Override
            public void onReceive(Context context, Intent intent)
            {
                int streamType = intent.getIntExtra(
                    "android.media.EXTRA_VOLUME_STREAM_TYPE", -1);
                int volume = intent.getIntExtra(
                    "android.media.EXTRA_VOLUME_STREAM_VALUE", -1);

                if (streamType == RockboxPCM.streamtype &&
                    volume != -1 &&
                    volume != setstreamvolume &&
                    rbservice.isRockboxRunning())
                {
                    postVolume(volume);
                }
            }
        };

        /* at startup, change the internal rockbox volume to what the global
           android music stream volume is */
        int volume = audiomanager.getStreamVolume(streamtype);
        int rbvolume = ((maxstreamvolume - volume) * -99) / maxstreamvolume;
        postVolumeChangedEvent(rbvolume);

        /* We're relying on internal API's here,
           this can break in the future! */
        rbservice.registerReceiver(
            broadcastReceiver,
            new IntentFilter("android.media.VOLUME_CHANGED_ACTION"));
    }

    private int bytes2frames(int bytes) 
    {
        /* 1 sample is 2 bytes, 2 samples are 1 frame */
        return (bytes/4);
    }

    private int frames2bytes(int frames) 
    {
        /* 1 frame is 2 samples, 1 sample is 2 bytes */
        return (frames*4);
    }

    private void play_pause(boolean pause) 
    {
        RockboxService service = RockboxService.getInstance();
        if (pause)
        {
            Intent widgetUpdate = new Intent("org.rockbox.UpdateState");
            widgetUpdate.putExtra("state", "pause");
            service.sendBroadcast(widgetUpdate);
            service.stopForeground();
            pause();
        }
        else
        {
            Intent widgetUpdate = new Intent("org.rockbox.UpdateState");
            widgetUpdate.putExtra("state", "play");
            service.sendBroadcast(widgetUpdate);
            service.startForeground();
            if (getPlayState() == AudioTrack.PLAYSTATE_STOPPED)
            {
                /* need to fill with silence before starting playback */ 
                write(raw_data, 0, raw_data.length);
            }
            play();
        }
    }

    @Override
    public void play() throws IllegalStateException
    {
        super.play();
        /* when stopped or paused the streamer is in a wait() state. need
         * it to wake it up */
        streamer.kick();
    }

    @Override
    public synchronized void stop() throws IllegalStateException 
    {
        /* flush pending data, but turn the volume off so it cannot be heard.
         * This is so that we don't hear old data if music is resumed very
         * quickly after (e.g. when seeking).
         */
        float old_vol = curpcmvolume; 
        try {
            setStereoVolume(0, 0);
            flush();
            super.stop();
        } catch (IllegalStateException e) {
            throw new IllegalStateException(e);
        } finally {
            setStereoVolume(old_vol, old_vol);
        }

        Intent widgetUpdate = new Intent("org.rockbox.UpdateState");
        widgetUpdate.putExtra("state", "stop");
        RockboxService.getInstance().sendBroadcast(widgetUpdate);
        RockboxService.getInstance().stopForeground();
    }

    @Override
    public void release()
    {
        super.release();
        /* stop streamer if this AudioTrack is destroyed by whomever */
        streamer.quitAndJoin();
    }

    public int setStereoVolume(float leftVolume, float rightVolume)
    {
        curpcmvolume = leftVolume;
        return super.setStereoVolume(leftVolume, rightVolume);
    }

    private void set_volume(int volume)
    {
        Logger.d("java:set_volume("+volume+")");
        /* Rockbox volume is centi-bel attenuation: 0 = full, -990 ≈ mute.
         *
         * The old mapping took that range and linearly chose an Android
         * STREAM_MUSIC step from it, which made the perceived curve
         * lopsided — each stream step is already roughly logarithmic in
         * amplitude on this device, so a linear preimage gave "no
         * change at the top, big jumps at the bottom".
         *
         * Now we convert centi-bel → linear amplitude gain
         *      gain = 10^(volume / 200)
         * and split that across the Android stream step (coarse — what
         * the hardware buttons and volume HUD track) and PCM stereo
         * gain (fine — fills smoothly between stream steps).  The
         * stream step is chosen so its nominal amplitude is just at
         * or above the target gain, and the PCM gain trims it back
         * down to land on the target exactly.  Result: smooth
         * perceptual control across the whole range. */
        if (volume < -990) volume = -990;
        if (volume > 0)    volume = 0;

        /* Two-zone curve.  The dial maps the rockbox volume range
         * [-990, 0] cB to a position t ∈ [0, 1].
         *
         *   t ∈ [0, 0.20]  — anteroom.  Linear-in-amplitude fade from
         *                    true silence up to the audibility floor.
         *                    Most of this zone is below the device's
         *                    audible threshold, giving the bottom of
         *                    the dial a long quiet tail.
         *
         *   t ∈ [0.20, 1]  — listening band.  Linear-in-dB ramp from
         *                    the floor (~-30 dB) up to 0 dB at the
         *                    very top.  Each press changes the gain
         *                    by an equal number of dB, so the ramp
         *                    feels perceptually even / linear.
         *
         * Total audible range: 30 dB spread over 80% of the dial. */
        final float TOTAL_DB    = 40.0f;
        final float BREAKPOINT  = 0.20f;
        final float FLOOR_GAIN  = (float)Math.pow(10.0, -TOTAL_DB / 20.0);
        float t = (volume + 990) / 990.0f;
        float gain;
        if (volume <= -990) {
            gain = 0f;
        } else if (t <= BREAKPOINT) {
            gain = FLOOR_GAIN * (t / BREAKPOINT);
        } else {
            float band_t = (t - BREAKPOINT) / (1.0f - BREAKPOINT);
            float dB     = -TOTAL_DB + band_t * TOTAL_DB;
            gain         = (float)Math.pow(10.0, dB / 20.0);
        }

        int   idx;
        float pcm_scalar;
        if (gain <= 0f) {
            /* Y1 quirk: dropping the Android stream volume all the way
             * to 0 backgrounds org.rockbox shortly after — focus is
             * yanked, the framebuffer surface is destroyed, input
             * stops reaching us.  Floor the stream at 1 and use PCM
             * gain to reach real silence. */
            idx = 1;
            pcm_scalar = 0f;
        } else {
            idx = (int)Math.ceil(gain * maxstreamvolume);
            if (idx < 1)               idx = 1;
            if (idx > maxstreamvolume) idx = maxstreamvolume;
            pcm_scalar = (gain * maxstreamvolume) / idx;
            if (pcm_scalar > 1f) pcm_scalar = 1f;
        }

        int oldstreamvolume = audiomanager.getStreamVolume(streamtype);
        if (idx != oldstreamvolume) {
            Logger.d("java:setStreamVolume("+idx+")");
            setstreamvolume = idx;
            audiomanager.setStreamVolume(streamtype, idx, 0);
        }

        setStereoVolume(pcm_scalar * curleftvol, pcm_scalar * currightvol);
    }

    private void set_balance(int balance_i){
        float mLeftVol = 1.0f;
        float mRightVol = 1.0f;
        float balance = (float) balance_i / 100.0f;

        // left
        if (balance < 0) {
            mLeftVol = 1.0f;
            mRightVol = (1.0f + balance);
        // right
        } else {
            mLeftVol = (1.0f - balance);
            mRightVol = 1.0f;
        }
        setStereoVolume(mLeftVol, mRightVol);
        curleftvol = mLeftVol;
        currightvol = mRightVol;
    }

    public native int nativeWrite(byte[] temp, int len);
}

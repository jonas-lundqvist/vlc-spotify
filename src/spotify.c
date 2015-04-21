/*****************************************************************************
 * Copyright (C) 2015 Jonas Lundqvist
 *
 * Author: Jonas Lundqvist <jonas@gannon.se>
 *
 * This file is part of vlc-spotify.
 *
 * vlc-spotify is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include <stdlib.h>
#include <string.h>

#define VLC_MODULE_COPYRIGHT "Copyright (C) 2015 Jonas Lundqvist"
#define VLC_MODULE_LICENSE VLC_LICENSE_LGPL_2_1_PLUS

// VLC includes
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_demux.h>
#include <vlc_messages.h>
#include <vlc_threads.h>
#include <vlc_meta.h>
#include <vlc_dialog.h>

#include <libspotify/api.h>

#include "uriparser.h"

#define START_STOP_PROCEDURE_TIMEOUT_US 5000000

#ifndef _WIN32
#define VLC_SPOTIFY_CACHE_DIR "/tmp/spot"
#else
#define VLC_SPOTIFY_CACHE_DIR "C:\\temp\\spot"
#endif

typedef enum {
    CLEANUP_NOT_STARTED,
    CLEANUP_PENDING,
    CLEANUP_STARTED,
    CLEANUP_DONE,
} cleanup_state_e;

struct demux_sys_t {
    vlc_thread_t    thread;
    vlc_cond_t      wait;
    vlc_cond_t      spotify_wait;

    vlc_mutex_t     lock;
    vlc_mutex_t     audio_lock;
    vlc_mutex_t     cleanup_lock;

    bool            spotify_notification;
    bool            play_started;
    bool            format_set;
    bool            start_procedure_done;
    bool            start_procedure_succesful;

    cleanup_state_e cleanup;

    char           *psz_uri;

    char           *psz_meta_artist;
    char           *psz_meta_track;
    char           *psz_meta_album;

    es_out_id_t    *p_es_audio;
    date_t          pts;
    date_t          starttime;
    mtime_t         duration;
    mtime_t         pts_offset;;

    // TODO: Due to libspotify limitations there can be only one
    // sp_session. This one could be made persistant in the future.
    // That way we could handle loading of meta data for tracks/albums/etc
    // while playing one track.
    sp_session     *p_session;
    sp_track       *p_track;
};

extern const uint8_t g_appkey[];
extern const size_t g_appkey_size;

// Needed for VLC module
static int Open(vlc_object_t *object);
static void Close(vlc_object_t *object);

// Needed for VLC demux module
static int Control(demux_t *p_demux, int i_query, va_list args);
static int Demux(demux_t *p_demux);

static void *Run(void *data);
static void CleanupThread(void *data);
void set_track_meta(demux_sys_t *p_sys);

static SP_CALLCONV void spotify_logged_in(sp_session *sess, sp_error error);
static SP_CALLCONV void spotify_logged_out(sp_session *sess);
static SP_CALLCONV void spotify_log_message(sp_session *sess, const char *msg);
static SP_CALLCONV void spotify_notify_main_thread(sp_session *sess);
static SP_CALLCONV int spotify_music_delivery(sp_session *sess, const sp_audioformat *format,
                                              const void *frames, int num_frames);
static SP_CALLCONV void spotify_metadata_updated(sp_session *sess);
static SP_CALLCONV void spotify_message_to_user(sp_session *sess, const char *msg);
static SP_CALLCONV void spotify_play_token_lost(sp_session *sess);
static SP_CALLCONV void spotify_end_of_track(sp_session *sess);

static sp_session_callbacks spotify_session_callbacks = {
    .logged_in = &spotify_logged_in,
    .logged_out = &spotify_logged_out,
    .notify_main_thread = &spotify_notify_main_thread,
    .music_delivery = &spotify_music_delivery,
    .metadata_updated = &spotify_metadata_updated,
    .play_token_lost = &spotify_play_token_lost,
    .log_message = &spotify_log_message,
    .message_to_user = &spotify_message_to_user,
    .end_of_track = &spotify_end_of_track,
};

static sp_session_config spconfig = {
    .api_version = SPOTIFY_API_VERSION,
    .cache_location = VLC_SPOTIFY_CACHE_DIR, // TODO: path to vlc data?
    //.settings_location = "/tmp/spot", // Crashes at logout when enabled (!)
    .application_key = g_appkey,
    .application_key_size = 0,
    .user_agent = "vlc-spotify",
    .callbacks = &spotify_session_callbacks,
    NULL,
};

static const char * const pref_bitrate_text[] = { "96 kbps", "160 kbps", "320 kbps" };
static const sp_bitrate pref_bitrate[] = { SP_BITRATE_96k, SP_BITRATE_160k, SP_BITRATE_320k };

vlc_module_begin()
    set_shortname("Spotify")
    set_description("Stream from Spotify")
    set_capability("access_demux", 10)
    set_callbacks(Open, Close)
    set_category(CAT_INPUT)
    set_subcategory(SUBCAT_INPUT_ACCESS)
    // TODO: Handle spotify:// and file://
    // This will implicitly handle "vlc spotify:tra.." since file://<path> will be
    // prepended, although there is no real file.
    add_shortcut("spotify")
    add_string("spotify-username", "",
                "Username", "Spotify Username", false)
    add_password("spotify-password", "",
                "Password", "Spotify Password", false)
    add_integer("preferred_bitrate", SP_BITRATE_320k, "Preferred bitrate", "The preferred bitrate of the audio", true)
        change_integer_list(pref_bitrate, pref_bitrate_text)
    // TODO: Add 'spotify social'
vlc_module_end ()

static int Open(vlc_object_t *obj)
{
    demux_t     *p_demux = (demux_t *)obj;
    demux_sys_t *p_sys = calloc(1, sizeof(demux_sys_t));
    mtime_t      deadline;

    if (!p_sys)
        return VLC_ENOMEM;

    p_demux->p_sys = p_sys;

    // TODO, set p_sys->psz_uri as 2nd parameter
    spotify_type_e spotify_type = ParseURI(p_demux->psz_location, NULL);

    msg_Dbg(p_demux, "URI is %s", p_demux->psz_location);

    // TODO: Support more stuff then just tracks
    if (spotify_type != SPOTIFY_TRACK) {
        free(p_sys);
        return VLC_EGENERIC;
    }

    p_demux->pf_demux = Demux;
    p_demux->pf_control = Control;

    p_sys->start_procedure_done = false;
    p_sys->start_procedure_succesful = false;

    vlc_mutex_init(&p_sys->lock);
    vlc_mutex_init(&p_sys->cleanup_lock);
    vlc_mutex_init(&p_sys->audio_lock);
    vlc_cond_init(&p_sys->wait);

    p_sys->play_started = false;
    p_sys->cleanup = CLEANUP_NOT_STARTED;
    p_sys->psz_uri = strdup(p_demux->psz_location);
    p_sys->format_set = false;
    p_sys->p_session = NULL;
    p_sys->p_es_audio = NULL;
    p_sys->pts_offset = 0;

    p_sys->psz_meta_track = p_sys->psz_meta_artist = p_sys->psz_meta_album = NULL;

    // Create the thread that will handle the spotify activities
    if (vlc_clone(&p_sys->thread, Run, p_demux, VLC_THREAD_PRIORITY_LOW)) {
        vlc_cond_destroy(&p_sys->wait);
        vlc_mutex_destroy(&p_sys->lock);
        vlc_mutex_destroy(&p_sys->cleanup_lock);
        vlc_mutex_destroy(&p_sys->audio_lock);
        free(p_sys->psz_uri);
        free(p_sys);
        return VLC_ENOMEM;
    }

    // Wait until we are logged in and playing until we return SUCCESS
    // Or bail out after START_STOP_PROCEDURE_TIMEOUT_US
    deadline = mdate() + START_STOP_PROCEDURE_TIMEOUT_US;
    vlc_mutex_lock(&p_sys->lock);
    do {
        vlc_cond_timedwait(&p_sys->wait, &p_sys->lock, deadline);
    } while(mdate() < deadline && p_sys->start_procedure_done == false);
    vlc_mutex_unlock(&p_sys->lock);

    if (p_sys->start_procedure_succesful == false) {
        msg_Dbg(p_demux, "Failed to start...");
        Close(obj);

        return VLC_EGENERIC;
    }

    msg_Dbg(p_demux, "Started succesfully");
    return VLC_SUCCESS;
}

static void Close(vlc_object_t *obj)
{
    demux_t *p_demux = (demux_t*)obj;
    demux_sys_t *p_sys = p_demux->p_sys;
    mtime_t deadline;

    msg_Dbg(p_demux, "Closing down");

    // Tell the thread to start the cleanup
    vlc_mutex_lock(&p_sys->cleanup_lock);
    if (p_sys->cleanup != CLEANUP_DONE) {
        deadline = mdate() + (START_STOP_PROCEDURE_TIMEOUT_US);
        p_sys->cleanup = CLEANUP_PENDING;
        spotify_notify_main_thread(p_sys->p_session);
        while(p_sys->cleanup != CLEANUP_DONE && mdate() < deadline) {
            vlc_cond_timedwait(&p_sys->wait, &p_sys->cleanup_lock, deadline);
        }
    }
    vlc_mutex_unlock(&p_sys->cleanup_lock);

    // Cancel the thread
    vlc_cancel(p_sys->thread);
    vlc_join(p_sys->thread, NULL);

    if (p_sys->p_session) {
        msg_Dbg(p_demux, "> sp_session_release()");
        sp_session_release(p_sys->p_session);
    }

    if (p_sys->p_es_audio)
        es_out_Del(p_demux->out, p_sys->p_es_audio);

    vlc_cond_destroy(&p_sys->wait);
    vlc_mutex_destroy(&p_sys->lock);
    vlc_mutex_destroy(&p_sys->cleanup_lock);
    vlc_mutex_destroy(&p_sys->audio_lock);

    // TODO: Create a function for this when adding more meta data
    if (p_sys->psz_meta_track)
        free(p_sys->psz_meta_track);
    if (p_sys->psz_meta_artist)
        free(p_sys->psz_meta_artist);
    if (p_sys->psz_meta_album)
        free(p_sys->psz_meta_album);

    free(p_sys->psz_uri);
    free(p_sys);
    msg_Dbg(p_demux, "Closed succesfully");
}

// Ugly hack. It seems like this is the only way to signal EOF
// TODO: es_out_Eos() might be something interesting...
static int Demux(demux_t *p_demux)
{
    demux_sys_t *p_sys = p_demux->p_sys;

    if (p_sys->p_es_audio == NULL && p_sys->format_set == true)
        return 0; // EOF, will close the module

#undef msleep
    // Sleep for 100 ms to not hammer the CPU
    msleep(100000);
    return 1;
}

static int Control(demux_t *p_demux, int i_query, va_list args)
{
    demux_sys_t *p_sys = p_demux->p_sys;
    bool *pb;
    bool b;
    int64_t i64;
    int64_t *pi64;
    double *pd;
    double d;
    vlc_meta_t *p_meta;

    switch(i_query)
    {
    case DEMUX_CAN_PAUSE:
    case DEMUX_CAN_SEEK:
        pb = (bool *) va_arg(args, bool *);
        *pb = true;
        return VLC_SUCCESS;

    case DEMUX_SET_PAUSE_STATE:
        b = (bool) va_arg(args, int);
        if (b) {
            // Pause
            vlc_mutex_lock(&p_sys->audio_lock);
            p_sys->pts_offset = p_sys->pts.date;
            msg_Dbg(p_demux, "> sp_session_player_play(%d)", !b);
            sp_session_player_play(p_sys->p_session, !b);
            vlc_mutex_unlock(&p_sys->audio_lock);
        } else {
            // Unpause
            vlc_mutex_lock(&p_sys->audio_lock);
            date_Set(&p_sys->pts, VLC_TS_0 + p_sys->pts_offset);
            date_Set(&p_sys->starttime, mdate() - p_sys->pts_offset);
            msg_Dbg(p_demux, "> sp_session_player_play(%d)", !b);
            sp_session_player_play(p_sys->p_session, !b);
            vlc_mutex_unlock(&p_sys->audio_lock);
        }

        return VLC_SUCCESS;

    case DEMUX_SET_TIME:
        i64 = (int64_t) va_arg(args, int64_t);
        vlc_mutex_lock(&p_sys->audio_lock);
        p_sys->pts_offset = i64;
        msg_Dbg(p_demux, "> sp_session_player_seek()");
        sp_session_player_seek(p_sys->p_session, p_sys->pts_offset / 1000);
        date_Set(&p_sys->pts, p_sys->pts_offset);
        date_Set(&p_sys->starttime, mdate() - p_sys->pts_offset);
        vlc_mutex_unlock(&p_sys->audio_lock);
        return VLC_SUCCESS;

    case DEMUX_GET_TIME:
        pi64 = (int64_t *) va_arg(args, int64_t *);
        *pi64 = date_Get(&p_sys->pts);
        return VLC_SUCCESS;

    case DEMUX_GET_POSITION:
        pd = (double *) va_arg(args, double *);
        *pd = (double) (p_sys->pts.date) / p_sys->duration;
        return VLC_SUCCESS;

    case DEMUX_SET_POSITION:
        d = (double) va_arg(args, double);
        vlc_mutex_lock(&p_sys->audio_lock);
        p_sys->pts_offset = (d * (p_sys->duration));
        msg_Dbg(p_demux, "> sp_session_player_seek()");
        sp_session_player_seek(p_sys->p_session, p_sys->pts_offset / 1000);
        date_Set(&p_sys->pts, p_sys->pts_offset);
        date_Set(&p_sys->starttime, mdate() - p_sys->pts_offset);
        vlc_mutex_unlock(&p_sys->audio_lock);
        return VLC_SUCCESS;

    case DEMUX_GET_PTS_DELAY:
        pi64 = (int64_t*) va_arg(args, int64_t *);
        *pi64 = INT64_C(1000) * var_InheritInteger(p_demux, "live-caching");
        return VLC_SUCCESS;

    case DEMUX_GET_LENGTH:
        pi64 = (int64_t*) va_arg(args, int64_t *);
        *pi64 = p_sys->duration;
        return VLC_SUCCESS;

    case DEMUX_CAN_CONTROL_PACE:
    case DEMUX_CAN_CONTROL_RATE:
        pb = (bool*) va_arg(args, bool *);
        *pb = true;
        return VLC_SUCCESS;

    case DEMUX_GET_META:
        p_meta = (vlc_meta_t*) va_arg(args, vlc_meta_t*);
        set_track_meta(p_sys);
        if (p_sys->psz_meta_track)
            vlc_meta_Set(p_meta, vlc_meta_Title, p_sys->psz_meta_track);
        if (p_sys->psz_meta_artist)
            vlc_meta_Set(p_meta, vlc_meta_Artist, p_sys->psz_meta_artist);
        if(p_sys->psz_meta_album)
            vlc_meta_Set(p_meta, vlc_meta_Album, p_sys->psz_meta_album);
        return VLC_SUCCESS;

    default:
        return VLC_EGENERIC;
    }

    return VLC_EGENERIC;
}

// TODO: Put the login and creation of the Spotify session somewhere else.
static void *Run(void *data)
{
    demux_t *p_demux = (demux_t *) data;
    demux_sys_t *p_sys = p_demux->p_sys;
    sp_error     err;
    int          canc = vlc_savecancel();
    char        *psz_username;
    char        *psz_password;
    int          spotify_timeout = 0;
    mtime_t      spotify_timeout_us;
    sp_bitrate   spotify_bitrate;

    psz_username = var_InheritString(p_demux, "spotify-username");
    psz_password = var_InheritString(p_demux, "spotify-password");

    p_sys->spotify_notification = false;
    vlc_cond_init(&p_sys->spotify_wait);

    spconfig.application_key_size = g_appkey_size;
    spconfig.userdata = p_demux;
    msg_Dbg(p_demux, "> sp_session_create()");
    err = sp_session_create(&spconfig, &p_sys->p_session);

    if (SP_ERROR_OK != err) {
        dialog_Fatal(p_demux, "Spotify session error: ", "%s", sp_error_message(err));
    }

    spotify_bitrate = var_InheritInteger(p_demux, "preferred_bitrate");
    msg_Dbg(p_demux, "> sp_session_preferred_bitrate(%d)", spotify_bitrate);
    err = sp_session_preferred_bitrate(p_sys->p_session, spotify_bitrate);
    if (SP_ERROR_OK != err) {
        msg_Dbg(p_demux, "Error setting the preferred bitrate");
    }

    msg_Dbg(p_demux, "> sp_session_login()");
    sp_session_login(p_sys->p_session, psz_username, psz_password, 0, NULL);

    for (;;) {
        vlc_mutex_lock(&p_sys->lock);
        vlc_cleanup_push(CleanupThread, p_demux);

        // Allow cancelation from here.
        vlc_restorecancel(canc);

        // Wait here until we get some expected spotify activity
        if (spotify_timeout == 0) {
            while (p_sys->spotify_notification == false) {
                msg_Dbg(p_demux, "Waiting for spotify activity");
                vlc_cond_wait(&p_sys->spotify_wait, &p_sys->lock);
            }
        } else if(p_sys->spotify_notification == false) {
            msg_Dbg(p_demux, "Waiting for timed spotify activity, %d ms", spotify_timeout);
            spotify_timeout_us = mdate() + spotify_timeout * 1000;
            vlc_cond_timedwait(&p_sys->spotify_wait, &p_sys->lock, spotify_timeout_us);
        }

        canc = vlc_savecancel();
        vlc_cleanup_pop();

        vlc_mutex_unlock(&p_sys->lock);

        // CLEANUP_PENDING is set from Close()
        vlc_mutex_lock(&p_sys->cleanup_lock);
        if (p_sys->cleanup == CLEANUP_PENDING) {
            msg_Dbg(p_demux, "> sp_track_release()");
            sp_track_release(p_sys->p_track);
            p_sys->p_track = NULL;
            msg_Dbg(p_demux, "> sp_player_unload()");
            sp_session_player_unload(p_sys->p_session);
            msg_Dbg(p_demux, "> sp_session_forget_me()");
            sp_session_forget_me(p_sys->p_session);
            msg_Dbg(p_demux, "> sp_session_logout()");
            sp_session_logout(p_sys->p_session);
            p_sys->cleanup = CLEANUP_STARTED;
        }
        // CLEANUP_DONE is set from the logout callback
        if (p_sys->cleanup == CLEANUP_DONE) {
            vlc_cond_signal(&p_sys->wait);
        }
        vlc_mutex_unlock(&p_sys->cleanup_lock);

        p_sys->spotify_notification = false;

        do {
            msg_Dbg(p_demux, "> sp_session_process_events()");
            sp_session_process_events(p_sys->p_session, &spotify_timeout);
        } while(spotify_timeout == 0);
    }

    return NULL;
}

static void CleanupThread(void *data)
{
    demux_t *p_demux = (demux_t *) data;
    demux_sys_t *p_sys = p_demux->p_sys;

    vlc_mutex_unlock(&p_sys->lock);
    msg_Dbg(p_demux, "Cleaning up the thread");

    vlc_cond_destroy(&p_sys->spotify_wait);
}

// Called from sp_session_process_events()
static SP_CALLCONV void spotify_logged_in(sp_session *sess, sp_error error)
{
    demux_t *p_demux = (demux_t *) sp_session_userdata(sess);
    demux_sys_t *p_sys = p_demux->p_sys;

    sp_link *link;
    msg_Dbg(p_demux, "< logged_in()");

    if (SP_ERROR_OK != error) {
        dialog_Fatal(p_demux, "Login Error: ","%s", sp_error_message(error));
        return;
    }

    link = sp_link_create_from_string(p_sys->psz_uri);
    msg_Dbg(p_demux, "> sp_track_add_ref(sp_link_as_track())");
    sp_track_add_ref(p_sys->p_track = sp_link_as_track(link));
    msg_Dbg(p_demux, "> sp_link_release()");
    sp_link_release(link);

    p_sys->format_set = false;
}

// Called from sp_session_process_events()
static SP_CALLCONV void spotify_logged_out(sp_session *sess)
{
    demux_t *p_demux = (demux_t *) sp_session_userdata(sess);
    demux_sys_t *p_sys = p_demux->p_sys;

    msg_Dbg(p_demux, "< logged_out()");

    p_sys->spotify_notification = true;
    p_sys->cleanup = CLEANUP_DONE;
}

// Called from sp_session_process_events()
static SP_CALLCONV void spotify_metadata_updated(sp_session *sess)
{
    demux_t *p_demux = (demux_t *) sp_session_userdata(sess);
    demux_sys_t *p_sys = p_demux->p_sys;

    msg_Dbg(p_demux, "< metadata_updated()");
    msg_Dbg(p_demux, "> sp_session_player_load()");
    vlc_mutex_lock(&p_sys->audio_lock);
    sp_session_player_load(p_sys->p_session, p_sys->p_track);
    msg_Dbg(p_demux, "> sp_session_player_play()");
    sp_session_player_play(p_sys->p_session, 1);
    p_sys->duration = sp_track_duration(p_sys->p_track)*1000;
    vlc_mutex_unlock(&p_sys->audio_lock);

    if (p_sys->play_started == false) {
        // Signal back that the start is done so Open() can return
        vlc_mutex_lock(&p_sys->lock);
        p_sys->start_procedure_done = true;
        p_sys->start_procedure_succesful = true;
        vlc_cond_signal(&p_sys->wait);
        p_sys->play_started = true;
        vlc_mutex_unlock(&p_sys->lock);
    }
}

// Called from sp_session_process_events()
static SP_CALLCONV void spotify_log_message(sp_session *sess, const char *msg)
{
    demux_t *p_demux = (demux_t *) sp_session_userdata(sess);

    msg_Dbg(p_demux, "< log_message(): %s", msg);
}

// Called from sp_session_process_events()
static SP_CALLCONV void spotify_message_to_user(sp_session *sess, const char *msg)
{
    demux_t *p_demux = (demux_t *) sp_session_userdata(sess);

    // TODO: What kind of messages is this?
    // Is perhaps a dialog needed?
    msg_Dbg(p_demux, "< message_to_user(): %s", msg);
}

// libspotify context
static SP_CALLCONV void spotify_notify_main_thread(sp_session *sess)
{
    demux_t *p_demux = (demux_t *) sp_session_userdata(sess);
    demux_sys_t *p_sys = p_demux->p_sys;

    msg_Dbg(p_demux, "< notify_main_thread()");
    vlc_mutex_lock(&p_sys->lock);
    p_sys->spotify_notification = true;
    vlc_cond_signal(&p_sys->spotify_wait);
    vlc_mutex_unlock(&p_sys->lock);
}

// libspotify context
static SP_CALLCONV void spotify_play_token_lost(sp_session *sess)
{
    demux_t *p_demux = (demux_t *) sp_session_userdata(sess);
    demux_sys_t *p_sys = p_demux->p_sys;
    VLC_UNUSED(p_sys);

    msg_Dbg(p_demux, "< play_token_lost()");
    dialog_Fatal(p_demux, "Playtoken lost!", "Someone else is using your spotify account");

    // TODO: Any way to signal pause state to vlc core?
}

// libspotify context
static SP_CALLCONV void spotify_end_of_track(sp_session *sess)
{
    demux_t *p_demux = (demux_t *) sp_session_userdata(sess);
    demux_sys_t *p_sys = p_demux->p_sys;

    msg_Dbg(p_demux, "< end_of_track()");

    vlc_mutex_lock(&p_sys->audio_lock);
    if (p_sys->p_es_audio) {
        es_out_Del(p_demux->out, p_sys->p_es_audio);
        p_sys->p_es_audio = NULL;
    }
    vlc_mutex_unlock(&p_sys->audio_lock);
}

// libspotify context
static SP_CALLCONV int spotify_music_delivery(sp_session *sess, const sp_audioformat *format,
                                              const void *frames, int num_frames)
{
    demux_t *p_demux = (demux_t *) sp_session_userdata(sess);
    demux_sys_t *p_sys = p_demux->p_sys;
    mtime_t pts;
    block_t *p_block;
    int delivery_bytes;

    if (unlikely(num_frames == 0))
        return 0;

    vlc_mutex_lock(&p_sys->audio_lock);

    if (unlikely(p_sys->format_set == false)) {
        es_format_t fmt;
        es_format_Init(&fmt, AUDIO_ES, VLC_CODEC_S16N);
        fmt.audio.i_channels =  format->channels;
        fmt.audio.i_rate =  format->sample_rate;
        fmt.audio.i_bitspersample =  8 * sizeof(int16_t);
        fmt.audio.i_blockalign =  fmt.audio.i_bitspersample
                                * format->channels / 8;
        fmt.i_bitrate =  fmt.audio.i_rate
                       * fmt.audio.i_bitspersample
                       * fmt.audio.i_channels;

        p_sys->p_es_audio = es_out_Add(p_demux->out, &fmt);
        date_Init(&p_sys->pts, fmt.audio.i_rate, 1);
        date_Set(&p_sys->pts, VLC_TS_0);
        date_Set(&p_sys->starttime, mdate());
        p_sys->format_set = true;
    }

    pts = date_Get(&p_sys->pts);
    // Pace control, only feed up to 250 ms to ES
    if (pts - (mdate() - p_sys->starttime.date) > 250000) {
        vlc_mutex_unlock(&p_sys->audio_lock);
        return 0;
    }

    delivery_bytes = num_frames * format->channels * sizeof(int16_t);

    if (unlikely(p_sys->p_es_audio == NULL)) {
        vlc_mutex_unlock(&p_sys->audio_lock);
        return 0;
    }

    p_block = block_Alloc(delivery_bytes);

    if (unlikely(!p_block)) {
        vlc_mutex_unlock(&p_sys->audio_lock);
        return 0;
    }

    memcpy(p_block->p_buffer, frames, delivery_bytes);

    p_block->i_pts = p_block->i_dts = pts;
    p_block->i_length = date_Increment(&p_sys->pts, num_frames) - pts;
    p_block->i_buffer = delivery_bytes;
    p_block->i_nb_samples = num_frames * format->channels;

    es_out_Control(p_demux->out, ES_OUT_SET_PCR, pts);
    es_out_Send(p_demux->out, p_sys->p_es_audio, p_block);

    vlc_mutex_unlock(&p_sys->audio_lock);

    return num_frames;
}

void set_track_meta(demux_sys_t *p_sys)
{
    const char *track = sp_track_name(p_sys->p_track);
    sp_album *album;
    sp_artist *artist;

    if (track == NULL)
        return;

    if (p_sys->psz_meta_track == NULL)
        p_sys->psz_meta_track = strdup(track);

    album = sp_track_album(p_sys->p_track);

    if (p_sys->psz_meta_album == NULL && sp_album_name(album) != NULL)
        p_sys->psz_meta_album = strdup(sp_album_name(album));

    // Only fetch the 1st artist
    // TODO: Concatenate all artists
    artist = sp_track_artist(p_sys->p_track, 0);

    if (p_sys->psz_meta_artist == NULL && sp_artist_name(artist))
        p_sys->psz_meta_artist = strdup(sp_artist_name(artist));
}

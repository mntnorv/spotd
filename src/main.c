/*
 * The MIT License (MIT)
 * 
 * Copyright (c) 2015 Mantas Norvaiša
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * This file is part of spotd.
 */

#include <libgen.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>

#include <libspotify/api.h>

#include "types.h"
#include "audio.h"
#include "server.h"

/* --- Data --- */
// The application key is specific to each project, and allows Spotify
// to produce statistics on how our service is used.
extern const uint8_t g_appkey[];
// The size of the application key.
extern const size_t g_appkey_size;

// The output queue for audo data
static audio_fifo_t g_audiofifo;
// Synchronization mutex for the main thread
static pthread_mutex_t g_notify_mutex;
// Synchronization condition variable for the main thread
static pthread_cond_t g_notify_cond;
// Synchronization variable telling the main thread to process events
static int g_notify_do;
// Non-zero when a track has ended and a new one has not been started yet
static int g_playback_done;
// The global session handle
static sp_session *g_sess;
// The session playlist container
static sp_playlistcontainer *g_playlistcontainer;
// Handle to the current track
static sp_track *g_current_track;
// Handle to the queued track
static sp_track *g_queued_track;
// Handle to the command to be executed
static spotd_command *g_command;

// Set of signals to handle with handle_signals()
static sigset_t g_handled_signal_set;
// Synchronization variable telling the main thread to exit
static int g_interrupted;

/* --- Function definitions --- */
static sp_track *track_from_link(const char *link_str);
static spotd_error play_track(sp_track *track);
static void stop_playback(void);

/* ---------------------------  SESSION CALLBACKS  ------------------------- */

/**
 * This callback is called when an attempt to login has succeeded or failed.
 *
 * @sa sp_session_callbacks#logged_in
 */
static void logged_in(sp_session *sess, sp_error error) {
  if (SP_ERROR_OK != error) {
    fprintf(stderr, "Login failed: %s\n", sp_error_message(error));
    exit(2);
  }

  g_playlistcontainer = sp_session_playlistcontainer(sess);
}

/**
 * Callback called when libspotify has new metadata available
 *
 * @sa sp_session_callbacks#metadata_updated
 */
static void metadata_updated(sp_session *sess) {
  if (g_queued_track != NULL && g_current_track != g_queued_track) {
    play_track(g_queued_track);
    g_queued_track = NULL;
  }
}

/**
 * This callback is called from an internal libspotify thread to ask
 * us to reiterate the main loop.
 *
 * We notify the main thread using a condition variable and a protected variable.
 *
 * @sa sp_session_callbacks#notify_main_thread
 */
static void notify_main_thread(sp_session *sess) {
  pthread_mutex_lock(&g_notify_mutex);
  g_notify_do = 1;
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);
}

/**
 * This callback is used from libspotify whenever there is PCM data available.
 *
 * @sa sp_session_callbacks#music_delivery
 */
static int music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames) {
  audio_fifo_t *af = &g_audiofifo;
  audio_fifo_data_t *afd;
  size_t s;

  if (num_frames == 0) {
    return 0; // Audio discontinuity, do nothing
  }

  pthread_mutex_lock(&af->mutex);

  /* Buffer one second of audio */
  if (af->qlen > format->sample_rate) {
    pthread_mutex_unlock(&af->mutex);

    return 0;
  }

  s = num_frames * sizeof(int16_t) * format->channels;

  afd = malloc(sizeof(audio_fifo_data_t) + s);
  memcpy(afd->samples, frames, s);

  afd->nsamples = num_frames;

  afd->rate = format->sample_rate;
  afd->channels = format->channels;

  TAILQ_INSERT_TAIL(&af->q, afd, link);
  af->qlen += num_frames;

  pthread_cond_signal(&af->cond);
  pthread_mutex_unlock(&af->mutex);

  return num_frames;
}

/**
 * This callback is used from libspotify when the current track has ended
 *
 * @sa sp_session_callbacks#end_of_track
 */
static void end_of_track(sp_session *sess) {
  pthread_mutex_lock(&g_notify_mutex);
  g_playback_done = 1;
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);
}

/**
 * Notification that some other connection has started playing on this account.
 * Playback has been stopped.
 *
 * @sa sp_session_callbacks#play_token_lost
 */
static void play_token_lost(sp_session *sess) {
  stop_playback();
}

/**
 * This callback is called for log messages.
 *
 * @sa sp_session_callbacks#log_message
 */
static void log_message(sp_session *session, const char *data) {
  fprintf(stderr,"%s",data);
}

/**
 * The session callbacks
 */
static sp_session_callbacks session_callbacks = {
  .logged_in = &logged_in,
  .notify_main_thread = &notify_main_thread,
  .music_delivery = &music_delivery,
  .metadata_updated = &metadata_updated,
  .play_token_lost = &play_token_lost,
  .log_message = &log_message,
  .end_of_track = &end_of_track,
};

/**
 * The session configuration. Note that application_key_size is an
 * external, so we set it in main() instead.
 */
static sp_session_config spconfig = {
  .api_version = SPOTIFY_API_VERSION,
  .cache_location = "/tmp/spotd",
  .settings_location = "/tmp/spotd",
  .tracefile = NULL,
  .application_key = g_appkey,
  .application_key_size = 0, // Set in main()
  .user_agent = "spotd",
  .callbacks = &session_callbacks,
  NULL,
};

/* ---------------------------  SERVER CALLBACKS  -------------------------- */

/**
 * This callback handles commands received from clients
 *
 * @param  command  Command received from a client
 */
static void client_command_received (spotd_command *command) {
  pthread_mutex_lock(&g_notify_mutex);
  g_command = command;
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);
}

static spotd_server_callbacks server_callbacks = {
  .command_received = &client_command_received,
};

/* ---------------------------  PLAYBACK CONTROLS  ------------------------- */

/**
 * Creates an sp_track from a Spotify track link
 *
 * @param  link_str  The Spotify track link
 * @return an sp_track if the link passed was a valid link, NULL otherwise
 */
static sp_track *track_from_link(const char *link_str) {
  sp_link *link;
  sp_track *track;

  link = sp_link_create_from_string(link_str);

  if (link == NULL) {
    fprintf(stderr, "Error: \"%s\" is not a valid Spotify track link\n", link_str);
    return NULL;
  }

  sp_track_add_ref(track = sp_link_as_track(link));
  sp_link_release(link);

  return track;
}

/**
 * Play a track
 *
 * @param  link  The spotify link to the track
 */
static spotd_error play_track(sp_track *track) {
  sp_error track_error;

  if (g_current_track && g_current_track == track) {
    return SPOTD_ERROR_OK;
  }

  stop_playback();

  track_error = sp_track_error(track);

  if (track_error == SP_ERROR_OK) {
    g_current_track = track;
    printf("Now playing \"%s\"...\n", sp_track_name(track));
    
    sp_session_player_load(g_sess, g_current_track);
    sp_session_player_play(g_sess, 1);
  } else if (track_error == SP_ERROR_OTHER_PERMANENT) {
    printf("Failed trying to play track\n");
    return SPOTD_ERROR_OTHER_PERMANENT;
  } else if (track_error == SP_ERROR_IS_LOADING) {
    printf("Loading metadata for track...\n");
    g_queued_track = track;
  }

  /* Track not loaded? Then we need to wait for the metadata to
     load before we can start playback (see metadata_updated) */

  return SPOTD_ERROR_OK;
}

/**
 * Stop the currently playing track, if there is one
 */
static void stop_playback(void) {
  if (g_current_track != NULL) {
    audio_fifo_flush(&g_audiofifo);
    sp_session_player_unload(g_sess);
    sp_track_release(g_current_track);
    g_current_track = NULL;
  }
}

/* ---------------------------------  MAIN  -------------------------------- */

/**
 * A track has ended. Remove it from the playlist.
 *
 * Called from the main loop when the music_delivery() callback has set
 * g_playback_done.
 */
static void track_ended(void) {
  if (g_current_track) {
    printf("\"%s\" ended\n", sp_track_name(g_current_track));

    sp_track_release(g_current_track);
    g_current_track = NULL;
  }
}

/**
 * Show usage information
 *
 * @param  progname  The program name
 */
static void usage(const char *progname) {
  fprintf(stderr, "usage: %s -u <username> -p <password>\n", progname);
}

/**
 * Signal handler thread
 */
static void *signal_handler_thread(void *arg) {
  int sig;
  for (;;) {
    sigwait(&g_handled_signal_set, &sig);
    
    if (SIGINT == sig) {
      pthread_mutex_lock(&g_notify_mutex);
      g_interrupted = 1;
      pthread_cond_signal(&g_notify_cond);
      pthread_mutex_unlock(&g_notify_mutex);
    }
  }

  pthread_exit(NULL);
}

int main(int argc, char **argv) {
  sp_session *sp;
  sp_error err;
  sp_track *track;
  int next_timeout = 0;
  const char *username = NULL;
  const char *password = NULL;
  int opt;
  pthread_t signal_handler_thread_id;

  // Parse options
  while ((opt = getopt(argc, argv, "u:p:")) != EOF) {
    switch (opt) {
    case 'u':
      username = optarg;
      break;
    case 'p':
      password = optarg;
      break;
    default:
      exit(1);
    }
  }

  // Print usage if not all require arguments were passed
  if (!username || !password) {
    usage(basename(argv[0]));
    exit(1);
  }

  // Init global variables
  g_current_track = NULL;
  g_queued_track = NULL;

  // Initialize signal handling
  g_interrupted = 0;
  sigemptyset(&g_handled_signal_set);
  sigaddset(&g_handled_signal_set, SIGINT);
  pthread_sigmask(SIG_BLOCK, &g_handled_signal_set, NULL);
  pthread_create(&signal_handler_thread_id, NULL, signal_handler_thread, NULL);

  // Init the audio system
  audio_init(&g_audiofifo);

  // Start server
  if (spotd_server_start(8888, &server_callbacks) != SPOTD_ERROR_OK) {
    fprintf(stderr, "Error: %s\n", "failed starting a server");
    exit(1);
  }

  // Create session
  spconfig.application_key_size = g_appkey_size;

  err = sp_session_create(&spconfig, &sp);

  if (SP_ERROR_OK != err) {
    fprintf(stderr, "Unable to create session: %s\n", sp_error_message(err));
    exit(1);
  }

  g_sess = sp;

  pthread_mutex_init(&g_notify_mutex, NULL);
  pthread_cond_init(&g_notify_cond, NULL);

  sp_session_login(sp, username, password, 0, NULL);
  pthread_mutex_lock(&g_notify_mutex);

  for (;;) {
    if (next_timeout == 0) {
      while(!g_notify_do && !g_playback_done && !g_interrupted && (g_command == NULL)) {
        pthread_cond_wait(&g_notify_cond, &g_notify_mutex);
      }
    } else {
      struct timespec ts;

      #if _POSIX_TIMERS > 0
        clock_gettime(CLOCK_REALTIME, &ts);
      #else
        struct timeval tv;
        gettimeofday(&tv, NULL);
        TIMEVAL_TO_TIMESPEC(&tv, &ts);
      #endif

      ts.tv_sec += next_timeout / 1000;
      ts.tv_nsec += (next_timeout % 1000) * 1000000;

      pthread_cond_timedwait(&g_notify_cond, &g_notify_mutex, &ts);
    }

    g_notify_do = 0;
    pthread_mutex_unlock(&g_notify_mutex);

    if (g_interrupted) {
      // Stop execution if interrupted
      break;
    }

    if (g_playback_done) {
      track_ended();
      g_playback_done = 0;
    }

    if (g_command != NULL) {
      switch (g_command->type) {
      case SPOTD_COMMAND_PLAY_TRACK:
        track = track_from_link(g_command->argv[0]);
        if (track != NULL) {
          play_track(track);
        }
        break;
      case SPOTD_COMMAND_STOP:
        stop_playback();
        break;
      }

      spotd_command_release(g_command);
      g_command = NULL;
    }

    do {
      sp_session_process_events(sp, &next_timeout);
    } while (next_timeout == 0);

    pthread_mutex_lock(&g_notify_mutex);
  }

  // Cleanup
  stop_playback();
  spotd_server_stop();
  sp_playlistcontainer_release(g_playlistcontainer);
  sp_session_logout(g_sess);
  sp_session_release(g_sess);

  return 0;
}

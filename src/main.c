#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <libspotify/api.h>

#include "types.h"
#include "audio.h"
#include "server.h"

/* --- Data --- */
/// The application key is specific to each project, and allows Spotify
/// to produce statistics on how our service is used.
extern const uint8_t g_appkey[];
/// The size of the application key.
extern const size_t g_appkey_size;

/// The output queue for audo data
static audio_fifo_t g_audiofifo;
/// Synchronization mutex for the main thread
static pthread_mutex_t g_notify_mutex;
/// Synchronization condition variable for the main thread
static pthread_cond_t g_notify_cond;
/// Synchronization variable telling the main thread to process events
static int g_notify_do;
/// Non-zero when a track has ended and a new one has not been started yet
static int g_playback_done;
/// Non-zero when a new track needs to be played
static int g_play_track;
/// The global session handle
static sp_session *g_sess;
/// Handle to the curren track
static sp_track *g_currenttrack;
/// Track link to play
static char *g_track_link;

/* --- Function definitions --- */
static sp_error play_track(const char *link_str);
static void stop_playback(void);
static char *strip_str(const char *str, const char *d);

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
}

/**
 * Callback called when libspotify has new metadata available
 *
 * @sa sp_session_callbacks#metadata_updated
 */
static void metadata_updated(sp_session *sess) {
  puts("Metadata updated, trying to start playback");

  if (sp_track_error(g_currenttrack) != SP_ERROR_OK) {
    return;
  }

  printf("Now playing \"%s\"...\n", sp_track_name(g_currenttrack));
  fflush(stdout);

  sp_session_player_load(g_sess, g_currenttrack);
  sp_session_player_play(g_sess, 1);
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
  .cache_location = "/tmp",
  .settings_location = "/tmp",
  .tracefile = NULL,
  .application_key = g_appkey,
  .application_key_size = 0, // Set in main()
  .user_agent = "spotify-playtrack-example",
  .callbacks = &session_callbacks,
  NULL,
};

/* ---------------------------  SERVER CALLBACKS  -------------------------- */

static void client_command_received (const char* command) {
  g_track_link = strip_str(command, "\r\n");
  printf("Command received: %s\n", g_track_link);

  pthread_mutex_lock(&g_notify_mutex);
  g_play_track = 1;
  pthread_cond_signal(&g_notify_cond);
  pthread_mutex_unlock(&g_notify_mutex);
}

static spotd_server_callbacks server_callbacks = {
  .command_received = &client_command_received,
};

/* ---------------------------  PLAYBACK CONTROLS  ------------------------- */

/**
 * Play a track
 *
 * @param  link  The spotify link to the track
 */
static sp_error play_track(const char *link_str) {
  sp_link *link;

  stop_playback();

  printf("Loading \"%s\"...\n", link_str);
  link = sp_link_create_from_string(link_str);
  sp_track_add_ref(g_currenttrack = sp_link_as_track(link));
  sp_link_release(link);

  sp_error track_error = sp_track_error(g_currenttrack);

  if (track_error == SP_ERROR_OK) {
    printf("Now playing \"%s\"...\n", sp_track_name(g_currenttrack));
    fflush(stdout);
    
    sp_session_player_load(g_sess, g_currenttrack);
    sp_session_player_play(g_sess, 1);
  } else if (track_error == SP_ERROR_IS_LOADING) {
    printf("Loading metadata for \"%s\"...\n", link_str);
  } else if (track_error == SP_ERROR_OTHER_PERMANENT) {
    printf("Failed trying to play \"%s\"\n", link_str);
  }

  /* Track not loaded? Then we need to wait for the metadata to
     load before we can start playback (see metadata_updated) */

  return sp_track_error(g_currenttrack);
}

/**
 * Stop the currently playing track, if there is one
 */
static void stop_playback(void) {
  audio_fifo_flush(&g_audiofifo);

  if (g_currenttrack != NULL) {
    sp_session_player_unload(g_sess);
    g_currenttrack = NULL;
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
  if (g_currenttrack) {
    printf("\"%s\" ended\n", sp_track_name(g_currenttrack));

    sp_track_release(g_currenttrack);
    g_currenttrack = NULL;
  }
}

/**
 * Show usage information
 *
 * @param  progname  The program name
 */
static void usage(const char *progname) {
  fprintf(stderr, "usage: %s -u <username> -p <password> -t <track>\n", progname);
}

/**
 * Remove characters from a string.
 * The resulting string must be freed.
 *
 * @param  str  The string to strip
 * @param  d  The characters to strip
 */
static char *strip_str(const char *str, const char *d) {
  size_t length = strlen(str);
  char *stripped = (char *) malloc(length + 1);
  int stripped_len = 0, i;

  for (i = 0; i < length; i++) {
    if (!strchr(d, str[i])) {
      stripped[stripped_len] = str[i];
      stripped_len++;
    }
  }

  stripped[stripped_len] = '\0';
  return stripped;
}

int main(int argc, char **argv) {
  sp_session *sp;
  sp_error err;
  int next_timeout = 0;
  const char *username = NULL;
  const char *password = NULL;
  int opt;

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

  if (!username || !password) {
    usage(basename(argv[0]));
    exit(1);
  }

  audio_init(&g_audiofifo);

  /* Start server */
  start_server(8888, &server_callbacks);

  /* Create session */
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
      while(!g_notify_do && !g_playback_done) {
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

    if (g_playback_done) {
      track_ended();
      g_playback_done = 0;
    }

    if (g_play_track) {
      play_track(g_track_link);
      free(g_track_link);
      g_play_track = 0;
    }

    do {
      sp_session_process_events(sp, &next_timeout);
    } while (next_timeout == 0);

    pthread_mutex_lock(&g_notify_mutex);
  }

  return 0;
}

#include "audio.h"
#include <stdlib.h>

audio_fifo_data_t* audio_get(audio_fifo_t *af) {
  audio_fifo_data_t *afd;
  pthread_mutex_lock(&af->mutex);

  while (!(afd = TAILQ_FIRST(&af->q)))
  pthread_cond_wait(&af->cond, &af->mutex);

  TAILQ_REMOVE(&af->q, afd, link);
  af->qlen -= afd->nsamples;

  pthread_mutex_unlock(&af->mutex);
  return afd;
}

void audio_fifo_flush(audio_fifo_t *af) {
  audio_fifo_data_t *afd;

  pthread_mutex_lock(&af->mutex);

  while((afd = TAILQ_FIRST(&af->q))) {
    TAILQ_REMOVE(&af->q, afd, link);
    free(afd);
  }

  af->qlen = 0;
  pthread_mutex_unlock(&af->mutex);
}

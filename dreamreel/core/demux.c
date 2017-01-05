#include <kos.h>

#include "dreamreel.h"

/**************************************************************************
 * file globals
 **************************************************************************/

static int thread_is_alive;
static int demux_status;

/**************************************************************************
 * demuxer thread
 **************************************************************************/

void demux_thread(void *v) {

  buf_element_t *buf;
  xine_stream_t *stream = (xine_stream_t *)v;

  thread_is_alive = 0;
debug_printf ("  *** this is the demux thread talking\n");

  /* wait for the thread to become active initially */
debug_printf ("    demux: waiting for initial start signal\n");
  while (!thread_is_alive)
    thd_pass();

debug_printf ("    demux: thread is now alive\n");
  /* MRL has already been validated and opened at this point; send headers */
  stream->demux->send_headers(stream->demux);

  while (thread_is_alive) {

    /* dispatch a chunk while the demuxer still in playback state */
    if (demux_status == DEMUX_OK) {
debug_printf ("    demux: sending chunk\n");
      demux_status = stream->demux->send_chunk(stream->demux);

      /* if the state transitioned to DEMUX_FINISHED, user END_USER */
      if (demux_status == DEMUX_FINISHED) {
        thread_is_alive = 0;
      }
    }

    thd_pass();
  }

  /* let the decoders know that the show is over */
  buf = stream->video_fifo->buffer_pool_alloc(stream->video_fifo);
  buf->decoder_flags = BUF_FLAG_END_STREAM;
  stream->video_fifo->put(stream->video_fifo, buf);

  buf = stream->audio_fifo->buffer_pool_alloc(stream->audio_fifo);
  buf->decoder_flags = BUF_FLAG_END_STREAM;
  stream->audio_fifo->put(stream->audio_fifo, buf);

debug_printf ("demux thread exit\n");
}

void demux_thread_start(void) {

  thread_is_alive = 1;
  demux_status = DEMUX_OK;
  
debug_printf ("    demux: got the signal to start (status = %s)\n",
  (demux_status == DEMUX_OK) ? "OK" : "Finished");
}

void demux_thread_stop(void) {

  demux_status = DEMUX_FINISHED;
}

void demux_thread_exit(void) {

  thread_is_alive = 0;
}

int get_demux_status(void) {

  return demux_status;
}

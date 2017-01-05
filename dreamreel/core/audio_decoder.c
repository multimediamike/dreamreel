#include "dreamreel.h"

/**************************************************************************
 * audio decoder thread
 **************************************************************************/

void audio_decoder_thread(void *v) {

  xine_stream_t *stream = (xine_stream_t *)v;
  int end_of_stream;

debug_printf ("  *** this is the audio decoder thread talking\n");

  do {
    /* wait for a buffer */
    while (!mutex_is_locked(stream->audio_fifo->fifo_ready_mutex))
      thd_pass();

/*
debug_printf ("  audio thread received buffer, type %08X, %d bytes, flags: %08X\n",
  stream->audio_fifo->buf.type,
  stream->audio_fifo->buffer_data_index,
  stream->audio_fifo->buf.decoder_flags);
*/

    end_of_stream =
      stream->audio_fifo->buf.decoder_flags & BUF_FLAG_END_STREAM;

    stream->audio_fifo->clear(stream->audio_fifo);

  } while (!end_of_stream);

debug_printf ("audio decoder thread exit\n");
}


#include <stdio.h>
#include <kos.h>

#include "dreamreel.h"
#include "metronom.h"


//#define MRL "file://cd/film/miniop.cpk"
//#define MRL "file://cd/fli/test2.fli"
//#define MRL "file://cd/fli/fli_engines.fli"
#define MRL "file://cd/fli/crusher.flc"
//#define MRL "file://cd/fli/malev2.fli"
//#define MRL "file://cd/nsf/Legend_of_Zelda.nsf"
//#define MRL "file://cd/y4m/demoEnd.y4m"
//#define MRL "file://cd/eawve/networkBackbone.wve"
//#define MRL "file://cd/mov/971108_vis2_smc.mov"
//#define MRL "file://cd/idcin/idlog.cin"


KOS_INIT_FLAGS(INIT_DEFAULT | INIT_THD_PREEMPT);

void demux_thread(void *v);
void demux_thread_start(void);
void demux_thread_stop(void);
void demux_thread_exit(void);
int get_demux_status(void);
void video_decoder_thread(void *v);
void audio_decoder_thread(void *v);

void video_output_thread(void *v);

/**************************************************************************
 * "plugin" catalogs
 **************************************************************************/

extern void *cdfile_init_plugin (xine_t *xine, void *data);

plugin_info_t input_plugins[] = {
  /* type, API, "name", version, special_info, init_function, plugin_class */
  { PLUGIN_INPUT, 11, "cdfile", 1, NULL, cdfile_init_plugin, NULL }
};
#define NUM_INPUT_MODULES (sizeof(input_plugins) / sizeof(plugin_info_t))

extern void *demux_film_init_plugin (xine_t *xine, void *data);
extern void *demux_fli_init_plugin (xine_t *xine, void *data);
extern void *demux_idcin_init_plugin (xine_t *xine, void *data);
extern void *demux_yuv4mpeg2_init_plugin (xine_t *xine, void *data);

plugin_info_t demux_plugins[] = {
  /* type, API, "name", version, special_info,init_function, plugin_class */
  { PLUGIN_DEMUX, 20, "FILM", 1, NULL, demux_film_init_plugin, NULL},
  { PLUGIN_DEMUX, 20, "FLI", 1, NULL, demux_fli_init_plugin, NULL },
  { PLUGIN_DEMUX, 20, "Id CIN", 1, NULL, demux_idcin_init_plugin, NULL },
  { PLUGIN_DEMUX, 20, "YUV4MPEG2", 1, NULL, demux_yuv4mpeg2_init_plugin, NULL }
};
#define NUM_DEMUX_MODULES (sizeof(demux_plugins) / sizeof(plugin_info_t))

/**************************************************************************
 * core functions
 **************************************************************************/

void *xine_xmalloc(size_t size) {
  void *ptr;

  /* prevent xine_xmalloc(0) of possibly returning NULL */
  if( !size )
    size++;

  if((ptr = calloc(1, size)) == NULL) {
    printf("   ***** help! %s: could not allocate %d bytes\n", __func__, size);

while(1);

    return NULL;
  }

  return ptr;
}

void xine_log (xine_t *self, int buf,
               const char *format, ...) {
}

void xine_demux_flush_engine (xine_stream_t *stream) {

  debug_printf ("  xine_demux_flush_engine()\n");

}

void xine_demux_control_newpts (xine_stream_t *stream, int64_t pts, uint32_t flags) {

  debug_printf ("  xine_demux_control_newpts()\n");

  metronom_set(pts);
}

void xine_demux_control_headers_done (xine_stream_t *stream) {

  debug_printf ("  xine_demux_control_headers_done()\n");

}

void xine_demux_control_start (xine_stream_t *stream) {

  debug_printf ("  xine_demux_control_start()\n");

  xine_demux_control_newpts(stream, 0, 0);
}

void xine_demux_control_end (xine_stream_t *stream, uint32_t flags) {

  debug_printf ("  xine_demux_control_end()\n");

}

int  xine_config_lookup_entry (xine_t *self, const char *key,
                               xine_cfg_entry_t *entry) {

  debug_printf ("xine_config_lookup_entry()\n");
  return 0;
}

void xine_event_send (xine_stream_t *stream, const xine_event_t *event) {

  debug_printf ("xine_event_send()\n");

}

/**************************************************************************
 * buffer stuff
 **************************************************************************/

void buf_element_put (fifo_buffer_t *fifo, buf_element_t *buf) {

  /* sanity check the returned data */
  if (buf->size <= BUFFER_SIZE)
    fifo->buffer_data_index += buf->size;

  fifo->buf_allocated = 0;

  /* if the buffer is one of these special types, it is time to process
   * the buffer */
  if ((buf->decoder_flags & BUF_FLAG_FRAME_END) ||
      (buf->decoder_flags & BUF_FLAG_HEADER) ||
      (buf->decoder_flags & BUF_FLAG_PREVIEW) ||
      (buf->decoder_flags & BUF_FLAG_END_STREAM)) {

    mutex_lock(fifo->fifo_ready_mutex);
  }
}

buf_element_t *buf_element_get (fifo_buffer_t *fifo) {

  debug_printf ("  **** buf_element_get() unimplemented\n");

  return NULL;
}

void buf_element_clear (fifo_buffer_t *fifo) {

  fifo->buffer_data_index = 0;
  mutex_unlock(fifo->fifo_ready_mutex);
}

int buf_element_size (fifo_buffer_t *fifo) {

  debug_printf ("  **** buf_element_size() unimplemented\n");

  return 0;
}

int buf_element_num_free (fifo_buffer_t *fifo) {

  debug_printf ("  **** buf_element_num_free() unimplemented\n");

  return 0;
}

uint32_t buf_element_data_size (fifo_buffer_t *fifo) {

  debug_printf ("  **** buf_element_data_size() unimplemented\n");

  return 0;
}

void buf_element_dispose (fifo_buffer_t *fifo) {

  free(fifo->buffer_data);
  free(fifo);
}

void buf_element_free_buffer (buf_element_t *buf) {

  fifo_buffer_t *fifo = buf->source;

  fifo->buf_allocated = 0;
}

buf_element_t *buf_element_buffer_pool_alloc (fifo_buffer_t *fifo) {

  /* there is only one buffer to be allocated */
  if (fifo->buf_allocated) {
    printf ("  *********** help! single buffer resource already allocated!\n");
    for (;;)
      ;
  }

  /* only proceed if fifo is unlocked */
  while (mutex_is_locked(fifo->fifo_ready_mutex))
    thd_pass();

  /* make sure there is enough space for the buffer */
  if (fifo->buffer_data_index + BUFFER_SIZE > fifo->buffer_data_size) {
    fifo->buffer_data_size += DATA_ALLOC_INCREMENT;
    fifo->buffer_data = realloc(fifo->buffer_data, fifo->buffer_data_size);
  }

  /* load up the buffer structure */
  fifo->buf.next = NULL;
  fifo->buf.content = fifo->buf.mem = 
    &fifo->buffer_data[fifo->buffer_data_index];
  fifo->buf.size = 0;
  fifo->buf.max_size = BUFFER_SIZE;
  fifo->buf.type = 0;
  fifo->buf.pts = 0;
  fifo->buf.disc_off = 0;
  fifo->buf.extra_info = &fifo->extra_info;
  fifo->buf.decoder_flags = 0;
  fifo->buf.decoder_info[0] = 0;
  fifo->buf.decoder_info[1] = 0;
  fifo->buf.decoder_info[2] = 0;
  fifo->buf.decoder_info[3] = 0;
  fifo->buf.decoder_info_ptr[0] = NULL;
  fifo->buf.decoder_info_ptr[1] = NULL;
  fifo->buf.decoder_info_ptr[2] = NULL;
  fifo->buf.decoder_info_ptr[3] = NULL;
  fifo->buf.free_buffer = buf_element_free_buffer;
  fifo->buf.source = fifo;

  fifo->buf_allocated = 1;

  /* pass it back */
  return &fifo->buf;
}

fifo_buffer_t *init_fifo_buffer_t(void) {

  fifo_buffer_t *fifo;

  fifo = xine_xmalloc(sizeof(fifo_buffer_t));

  fifo->buffer_data_size = DATA_ALLOC_INCREMENT;
  fifo->buffer_data = xine_xmalloc(fifo->buffer_data_size);
  fifo->buffer_data_index = 0;

  fifo->buf_allocated = 0;
  fifo->fifo_ready_mutex = mutex_create();

  fifo->put = buf_element_put;
  fifo->get = buf_element_get;
  fifo->clear = buf_element_clear;
  fifo->size = buf_element_size;
  fifo->num_free = buf_element_num_free;
  fifo->data_size = buf_element_data_size;
  fifo->dispose = buf_element_dispose;
  fifo->buffer_pool_alloc = buf_element_buffer_pool_alloc;

  return fifo;
}

/**************************************************************************
 * Dreamreel engine
 **************************************************************************/

void init_xine_t(xine_t *xine) {
}

void init_xine_stream_t(xine_stream_t *stream) {

  stream->content_detection_method = METHOD_BY_CONTENT;

  stream->video_fifo = init_fifo_buffer_t();
  stream->audio_fifo = init_fifo_buffer_t();
}

void init_modules(xine_t *xine) {

  int i;

  for (i = 0; i < NUM_INPUT_MODULES; i++) {
    input_plugins[i].plugin_class = input_plugins[i].init(xine, NULL);
  }

  for (i = 0; i < NUM_DEMUX_MODULES; i++) {
    demux_plugins[i].plugin_class = demux_plugins[i].init(xine, NULL);
  }

}

/* Look for an input module to handle this MRL */
void find_input_module(xine_stream_t *stream, char *mrl) {

  int i;

  for (i = 0; i < NUM_INPUT_MODULES; i++) {
    debug_printf ("    attempting module #%d (%s)\n", i, input_plugins[i].id);
    stream->input = 
      ((input_class_t *)
        input_plugins[i].plugin_class)->open_plugin(input_plugins[i].plugin_class, 
        stream, mrl);

    if (stream->input)
      break;
  }

  if (stream->input)
    debug_printf ("  input: %s\n",
      ((input_class_t *)input_plugins[i].plugin_class)->get_description((input_class_t *)input_plugins[i].plugin_class));
  else
    debug_printf ("  input: no input module found\n");

}

/* Look for a demux module to handle this MRL */
void find_demux_module(xine_stream_t *stream, char *mrl) {

  int i;

  for (i = 0; i < NUM_DEMUX_MODULES; i++) {
    debug_printf ("    attempting module #%d (%s)\n", i, demux_plugins[i].id);
    stream->demux = 
      ((demux_class_t *)
        demux_plugins[i].plugin_class)->open_plugin(demux_plugins[i].plugin_class, 
        stream, stream->input);

    if (stream->demux)
      break;
  }

  if (stream->demux)
    debug_printf ("  demux: %s\n",
      ((demux_class_t *)demux_plugins[i].plugin_class)->get_description((demux_class_t *)demux_plugins[i].plugin_class));
  else
    debug_printf ("  demux: no demux module found\n");

}

void register_decoders(void) {

  /* init the ffmpeg lavc subsystem */
  avcodec_init();

  /* register the ffmpeg lavc video decoders */
  register_avcodec(&cyuv_decoder);
  register_avcodec(&flic_decoder);
  register_avcodec(&idcin_decoder);

}

/**************************************************************************
 * main launching point
 **************************************************************************/

int main() {

  xine_t xine;
  xine_stream_t stream;
  cont_cond_t cont;

  debug_printf ("Dreamreel: %s\n", MRL);

  register_decoders();

  init_metronom();

  init_xine_t(&xine);
  init_xine_stream_t(&stream);

  /* spin off the video output thread */
  stream.video_output_thread = thd_create(video_output_thread, &stream);
  thd_set_label(stream.video_output_thread, "video output thread");

debug_printf ("  init_modules()\n");
  init_modules(&xine);
debug_printf ("  find_input_module()\n");
  find_input_module(&stream, MRL);
  if (!stream.input)
    return 1;
debug_printf ("  find_demux_module()\n");
  find_demux_module(&stream, MRL);
  if (!stream.demux)
    return 1;

/*
debug_printf (" file has %d x %d video\n", 
  stream.stream_info[XINE_STREAM_INFO_VIDEO_WIDTH],
  stream.stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT]);
*/

printf ("press green to go...\n");
  do {
    if (cont_get_cond(maple_first_controller(), &cont))
      debug_printf ("Error getting controller status\n");
    cont.buttons = ~cont.buttons;
    vid_waitvbl();
  } while (!(cont.buttons & CONT_Y));
  do {
    if (cont_get_cond(maple_first_controller(), &cont))
      debug_printf ("Error getting controller status\n");
    cont.buttons = ~cont.buttons;
    vid_waitvbl();
  } while (cont.buttons & CONT_Y);

  /* start the threads and let them run the show */
  stream.demux_thread = thd_create(demux_thread, &stream);
  thd_set_label(stream.demux_thread, "demux thread");
  stream.video_decoder_thread = thd_create(video_decoder_thread, &stream);
  thd_set_label(stream.video_decoder_thread, "video decoder thread");
  stream.audio_decoder_thread = thd_create(audio_decoder_thread, &stream);
  thd_set_label(stream.audio_decoder_thread, "audio decoder thread");

printf ("press red to stop...\n");
  demux_thread_start();

  do {
    int paused = 0;

    if (cont_get_cond(maple_first_controller(), &cont))
      printf ("Error getting controller status\n");
    cont.buttons = ~cont.buttons;

    if (cont.buttons & CONT_Y) {
      if (get_demux_status() == DEMUX_OK) {
        if (paused) {
          start_metronom();
          paused = 0;
        } else {
          stop_metronom();
          paused = 1;
        }
      } else {
        demux_thread_start();
      }
    }

    thd_pass();
  } while (!(cont.buttons & CONT_A) && (get_demux_status() == DEMUX_OK));

  stop_video_out_thread();

debug_printf (" Dreamreel: all threads have finished\n");


  /* shut everything down */
  stream.input->dispose(stream.input);
  stream.demux->dispose(stream.demux);

  return 0;
}

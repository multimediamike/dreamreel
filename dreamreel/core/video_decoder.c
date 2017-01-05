#include "dreamreel.h"
#include "bswap.h"

#include "common.h"
#include "avcodec.h"

/**************************************************************************
 * video decoder global variables, shared with video out
 **************************************************************************/

/* these are the actual dimensions of the image */
int actual_width, actual_height;

/* these are the texture dimensions of the image */
int texture_width, texture_height;
int log2_width, log2_height;
int texture_size;

/* pixel format defined in libavcodec */
enum PixelFormat pixel_format;

static AVCodecContext *context;
static AVCodec *decoder;

/**************************************************************************
 * video support functions
 **************************************************************************/

static int get_buffer(AVCodecContext *context, AVFrame *av_frame) {

  int ret = 0;

  if (context->pix_fmt == PIX_FMT_PAL8) {
printf ("  allocating a buffer with %d bytes, %d stride\n",
  texture_size, texture_width);
    av_frame->data[0] = malloc(texture_size);
    av_frame->data[1] = av_frame->data[2] = NULL;
    av_frame->linesize[0] = texture_width;
    av_frame->linesize[1] = av_frame->linesize[2] = 0;

    if (!av_frame->data[0]) {
      debug_printf ("help! couldn't allocate a frame for get_buffer()!\n");
      ret = 1;
    } else {
      memset(av_frame->data[0], 0, texture_size);
printf ("    get_buffer() allocated a buffer @ %p with stride %d\n", 
  av_frame->data[0], av_frame->linesize[0]);
    }
  }

  return ret;
}

static void release_buffer(struct AVCodecContext *context, AVFrame *av_frame) {

  if (context->pix_fmt == PIX_FMT_PAL8) {
    free(av_frame->data[0]);
    av_frame->data[0] = NULL;
  }

}

/* dig up the correct decoder and parameters; return 0 if a decoder was
 * found */
int map_decoder(xine_stream_t *stream, buf_element_t *buf) {

  int ret = 0;

  switch (buf->type) {

  case BUF_VIDEO_FLI:
    decoder = avcodec_find_decoder (CODEC_ID_FLIC);
    stream->meta_info[XINE_META_INFO_VIDEOCODEC]
        = strdup ("Autodesk Animator FLI/FLC");
    actual_width = LE_16(&buf->content[8]);
    actual_height = LE_16(&buf->content[10]);
    break;

  case BUF_VIDEO_IDCIN:
    decoder = avcodec_find_decoder (CODEC_ID_IDCIN);
    stream->meta_info[XINE_META_INFO_VIDEOCODEC]
        = strdup ("Quake II Cinematic Video");
    actual_width = BE_16(&buf->content[0]);
    actual_height = BE_16(&buf->content[2]);
    break;

  default:
    ret = 1;
    debug_printf ("no video decoder available\n");
    break;
  }

  return ret;
}

/* returns 0 if everything checked out */
int init_video_parameters(void) {

  int number_of_bits;
  int i;

  /* sanity check the requested pixel format */
  if (pixel_format > PIX_FMT_NB)
    return 1;

  /* sanity check the dimensions */
  if ((actual_width < 16) ||
      (actual_width > 1024) ||
      (actual_height < 16) ||
      (actual_height > 1024))
    return 1;

  /* figure out the texture dimensions by working out the nearest power
   * of 2 */
  number_of_bits = log2_width = 0;
  for (i = 0; i < 31; i++)
    if (actual_width & (1 << i)) {
      number_of_bits++;
      log2_width = i;
    }
  if (number_of_bits > 1)
    log2_width++;
  texture_width = 1 << log2_width;

  number_of_bits = log2_height = 0;
  for (i = 0; i < 31; i++)
    if (actual_height & (1 << i)) {
      number_of_bits++;
      log2_height = i;
    }
  if (number_of_bits > 1)
    log2_height++;
  texture_height = 1 << log2_height;

  texture_size = texture_width * texture_height;

  return 0;
}

/**************************************************************************
 * video decoder thread
 **************************************************************************/

#if 1

void video_decoder_thread(void *v) {

  xine_stream_t *stream = (xine_stream_t *)v;
  int end_of_stream = 0;

  int len;
  AVFrame av_frame;
  int got_picture;
  int offset;
  int last_frame;

debug_printf ("  *** this is the video decoder thread talking\n");

  context = NULL;
  decoder = NULL;

  context = avcodec_alloc_context();
  context->codec_tag = stream->stream_info[XINE_STREAM_INFO_VIDEO_FOURCC];
  context->get_buffer = get_buffer;
  context->release_buffer = release_buffer;

  while (!end_of_stream) {

    /* wait for a buffer */
    while (!mutex_is_locked(stream->video_fifo->fifo_ready_mutex))
      thd_pass();

debug_printf ("  video decoder received buffer, type %08X, %d bytes, flags: %08X\n",
  stream->video_fifo->buf.type,
  stream->video_fifo->buffer_data_index,
  stream->video_fifo->buf.decoder_flags);

    last_frame = 
      stream->video_fifo->buf.decoder_flags & BUF_FLAG_END_USER;
    end_of_stream = 
      stream->video_fifo->buf.decoder_flags & BUF_FLAG_END_STREAM;
    if (end_of_stream)
      continue;

    /* handle the header */
    if (stream->video_fifo->buf.decoder_flags & BUF_FLAG_HEADER) {

      map_decoder(stream, &stream->video_fifo->buf);
      context->width = actual_width;
      context->height = actual_height;

      /* Id CIN Huffman tables */
/*
      if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
          (buf->decoder_info[1] == BUF_SPECIAL_IDCIN_HUFFMAN_TABLE)) {
          avctx->extradata = av_malloc(65536);
          avctx->extradata_size = 65536;
          memcpy(avctx->extradata, buf->decoder_info_ptr[2], 65536);
      }
*/

      init_video_parameters();

      if (avcodec_open (context, decoder) < 0) {
        printf ("ffmpeg: couldn't open decoder\n");
        free(context);
        context = NULL;
        stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 0;
      } else {
        stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 1;
      }

      if (init_video_out() != 0) {
        printf (" *** could not initialize video out\n");
        return;
      }

      /* finished processing this buffer; wait for the next one */
      stream->video_fifo->clear(stream->video_fifo);
      continue;
    }

    /* handle any special buffers */
    if (stream->video_fifo->buf.decoder_flags & BUF_FLAG_SPECIAL) {

      /* finished processing this buffer; wait for the next one */
      stream->video_fifo->clear(stream->video_fifo);
      continue;
    }

    /* decode the video */
    lock_twiddle_texture();

    offset = 0;
    len = avcodec_decode_video (context, &av_frame,
      &got_picture, &stream->video_fifo->buffer_data[offset], 
      stream->video_fifo->buffer_data_index);

    draw_texture_slice(av_frame.data, 512, 0, 512, 256);

debug_printf ("  video decoder sending out a frame with pts %lld...\n", 
    stream->video_fifo->buf.pts);
    stream->video_fifo->clear(stream->video_fifo);
    send_texture(stream->video_fifo->buf.pts,
      stream->video_fifo->buf.pts, av_frame.new_palette, 
      av_frame.palette, last_frame);
  };

debug_printf ("video decoder thread exit\n");
}


#else


void video_decoder_thread(void *v) {

  xine_stream_t *stream = (xine_stream_t *)v;
  int end_of_stream = 0;
  int i;

  AVFrame av_frame;
  static int64_t pts = 0;
  static unsigned char first_pixel = 0;
  unsigned char pixel;
  int address;
  int x, y;
  static int color_inc = 0x01; /* initialize to blue */

debug_printf ("  *** this is the testbed video decoder thread talking\n");

  context = NULL;
  decoder = NULL;

  context = avcodec_alloc_context();
  context->codec_tag = stream->stream_info[XINE_STREAM_INFO_VIDEO_FOURCC];
  context->get_buffer = get_buffer;
  context->release_buffer = release_buffer;

  while (!end_of_stream) {

    /* wait for a buffer */
    while (!mutex_is_locked(stream->video_fifo->fifo_ready_mutex))
      thd_pass();

#if 1
debug_printf ("  video decoder received buffer, type %08X, %d bytes, flags: %08X\n",
  stream->video_fifo->buf.type,
  stream->video_fifo->buffer_data_index,
  stream->video_fifo->buf.decoder_flags);
#endif

    end_of_stream = 
      stream->video_fifo->buf.decoder_flags & BUF_FLAG_END_STREAM;
    if (end_of_stream)
      continue;

    /* handle the header */
    if (stream->video_fifo->buf.decoder_flags & BUF_FLAG_HEADER) {

      map_decoder(stream, &stream->video_fifo->buf);

      init_video_parameters();

      if (avcodec_open (context, decoder) < 0) {
        printf ("ffmpeg: couldn't open decoder\n");
        free(context);
        context = NULL;
        stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 0;
      } else {
        stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 1;
      }

      get_buffer(context, &av_frame);

      if (init_video_out() != 0) {
        printf (" *** could not initialize video out\n");
        return;
      }

      /* finished processing this buffer; wait for the next one */
      stream->video_fifo->clear(stream->video_fifo);
      continue;
    }

    /* handle any special buffers */
    if (stream->video_fifo->buf.decoder_flags & BUF_FLAG_SPECIAL) {

      /* finished processing this buffer; wait for the next one */
      stream->video_fifo->clear(stream->video_fifo);
      continue;
    }

    /* decode the video */
    lock_twiddle_texture();

debug_printf (" data @ %p, stride = %d, first pixel = %d\n", av_frame.data[0], av_frame.linesize[0], first_pixel);


    if (color_inc == 0x01000000) /* red */
      color_inc = 0x010101;  /* white */
    else if (color_inc == 0x01010100)  /* white */
      color_inc = 0x01;  /* reset to blue */
    av_frame.new_palette = color_inc;
    av_frame.palette[0] = 0;
    for (i = 1; i < 255; i++)
      av_frame.palette[i] = av_frame.palette[i - 1] + color_inc;
    color_inc <<= 8;

    for (y = 0; y < actual_height; y++) {
      pixel = first_pixel;
      address = y * av_frame.linesize[0];
      for (x = 0; x < actual_width; x++)
        av_frame.data[0][address + x] = pixel++;
    }

    first_pixel++;

    draw_texture_slice(av_frame.data, 512, 0, 512, 256);

debug_printf ("  video decoder sending out a frame with pts %lld...\n", pts);

    stream->video_fifo->clear(stream->video_fifo);
    send_texture(pts, pts, av_frame.new_palette, 
      av_frame.palette, end_of_stream);

    pts += 45000;

  };

debug_printf ("video decoder thread exit\n");
}

#endif

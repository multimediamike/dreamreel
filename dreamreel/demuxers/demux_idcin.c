/*
 * Copyright (C) 2000-2003 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * CIN File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Id CIN file format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 *
 * CIN is a somewhat quirky and ill-defined format. Here are some notes
 * for anyone trying to understand the technical details of this format:
 *
 * The format has no definite file signature. This is problematic for a
 * general-purpose media player that wants to automatically detect file
 * types. However, a CIN file does start with 5 32-bit numbers that
 * specify audio and video parameters. This demuxer gets around the lack
 * of file signature by performing sanity checks on those parameters.
 * Probabalistically, this is a reasonable solution since the number of
 * valid combinations of the 5 parameters is a very small subset of the
 * total 160-bit number space.
 *
 * Refer to the function demux_idcin_open() for the precise A/V parameters
 * that this demuxer allows.
 *
 * Next, each audio and video frame has a duration of 1/14 sec. If the
 * audio sample rate is a multiple of the common frequency 22050 Hz it will
 * divide evenly by 14. However, if the sample rate is 11025 Hz:
 *   11025 (samples/sec) / 14 (frames/sec) = 787.5 (samples/frame)
 * The way the CIN stores audio in this case is by storing 787 sample
 * frames in the first audio frame and 788 sample frames in the second
 * audio frame. Therefore, the total number of bytes in an audio frame
 * is given as:
 *   audio frame #0: 787 * (bytes/sample) * (# channels) bytes in frame
 *   audio frame #1: 788 * (bytes/sample) * (# channels) bytes in frame
 *   audio frame #2: 787 * (bytes/sample) * (# channels) bytes in frame
 *   audio frame #3: 788 * (bytes/sample) * (# channels) bytes in frame
 *
 * Finally, not all Id CIN creation tools agree on the resolution of the
 * color palette, apparently. Some creation tools specify red, green, and
 * blue palette components in terms of 6-bit VGA color DAC values which
 * range from 0..63. Other tools specify the RGB components as full 8-bit
 * values that range from 0..255. Since there are no markers in the file to
 * differentiate between the two variants, this demuxer uses the following
 * heuristic:
 *   - load the 768 palette bytes from disk
 *   - assume that they will need to be shifted left by 2 bits to
 *     transform them from 6-bit values to 8-bit values
 *   - scan through all 768 palette bytes
 *     - if any bytes exceed 63, do not shift the bytes at all before
 *       transmitting them to the video decoder
 *
 * $Id: demux_idcin.c,v 1.39 2003/04/26 20:16:08 guenter Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "demux.h"
#include "bswap.h"

#define IDCIN_HEADER_SIZE 20
#define HUFFMAN_TABLE_SIZE 65536
#define IDCIN_FRAME_PTS_INC  (90000 / 14)
#define PALETTE_SIZE 256

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  off_t                start;
  off_t                filesize;
  int                  status;

  unsigned int         video_width;
  unsigned int         video_height;
  unsigned int         audio_sample_rate;
  unsigned int         audio_bytes_per_sample;
  unsigned int         audio_channels;

  int                  audio_chunk_size1;
  int                  audio_chunk_size2;
  int                  current_audio_chunk;

  unsigned char        huffman_table[HUFFMAN_TABLE_SIZE];
  uint64_t             pts_counter;

  char                 last_mrl[1024];
} demux_idcin_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_idcin_class_t;

/* set DEBUG_IDCIN to output information about the A/V chunks that the
 * demuxer is dispatching to the engine */
#define DEBUG_IDCIN 0

#if DEBUG_IDCIN
#define debug_idcin printf
#else
static inline void debug_idcin(const char *format, ...) { }
#endif

static int demux_idcin_send_chunk(demux_plugin_t *this_gen) {

  demux_idcin_t *this = (demux_idcin_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int command;
  unsigned char preamble[8];
  unsigned char disk_palette[PALETTE_SIZE * 3];
  palette_entry_t palette[PALETTE_SIZE];
  int i;
  unsigned int remaining_sample_bytes;
  int scale_bits;

  /* figure out what the next data is */
  if (this->input->read(this->input, (unsigned char *)&command, 4) != 4) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  command = le2me_32(command);
  debug_idcin("  demux_idcin: command %d: ", command);
  if (command == 2) {
    debug_idcin("demux finished\n");
    this->status = DEMUX_FINISHED;
    return this->status;
  } else {
    if (command == 1) {
      debug_idcin("load palette\n");

      /* load a 768-byte palette and pass it to the demuxer */
      if (this->input->read(this->input, disk_palette, PALETTE_SIZE * 3) !=
        PALETTE_SIZE * 3) {
        this->status = DEMUX_FINISHED;
        return this->status;
      }

      /* scan the palette to figure out if it's 6- or 8-bit;
       * assume 6-bit palette until a value > 63 is seen */
      scale_bits = 2;
      for (i = 0; i < PALETTE_SIZE * 3; i++)
        if (disk_palette[i] > 63) {
          scale_bits = 0;
          break;
        }

      /* convert palette to internal structure */
      for (i = 0; i < PALETTE_SIZE; i++) {
        /* these are VGA color DAC values, which means they only range
         * from 0..63; adjust as appropriate */
        palette[i].r = disk_palette[i * 3 + 0] << scale_bits;
        palette[i].g = disk_palette[i * 3 + 1] << scale_bits;
        palette[i].b = disk_palette[i * 3 + 2] << scale_bits;
      }

      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->decoder_flags = BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
      buf->decoder_info[2] = PALETTE_SIZE;
      buf->decoder_info_ptr[2] = &palette;
      buf->size = 0;
      buf->type = BUF_VIDEO_IDCIN;
      this->video_fifo->put (this->video_fifo, buf);
    } else
      debug_idcin("load video and audio\n");
  }

  /* load the video frame */
  if (this->input->read(this->input, preamble, 8) != 8) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  remaining_sample_bytes = LE_32(&preamble[0]) - 4;

  debug_idcin("  demux_idcin: dispatching %d video bytes\n",
    remaining_sample_bytes);
  while (remaining_sample_bytes) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_VIDEO_IDCIN;
    buf->extra_info->input_pos = this->input->get_current_pos(this->input);
    buf->extra_info->input_length = this->filesize;
    buf->extra_info->input_time = this->pts_counter / 90;
    buf->pts = this->pts_counter;

    if (remaining_sample_bytes > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = remaining_sample_bytes;
    remaining_sample_bytes -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) !=
      buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      break;
    }

    /* all frames are intra-coded */
    buf->decoder_flags |= BUF_FLAG_KEYFRAME;
    if (!remaining_sample_bytes)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    debug_idcin("    sending video buf with %d bytes, %lld pts\n",
        buf->size, buf->pts);
    this->video_fifo->put(this->video_fifo, buf);
  }

  /* load the audio frame */
  if (this->audio_fifo && this->audio_sample_rate) {

    if (this->current_audio_chunk == 1) {
      remaining_sample_bytes = this->audio_chunk_size1;
      this->current_audio_chunk = 2;
    } else {
      remaining_sample_bytes = this->audio_chunk_size2;
      this->current_audio_chunk = 1;
    }

    debug_idcin("  demux_idcin: dispatching %d audio bytes\n",
      remaining_sample_bytes);
    while (remaining_sample_bytes) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_AUDIO_LPCM_LE;
      buf->extra_info->input_pos = this->input->get_current_pos(this->input);
      buf->extra_info->input_length = this->filesize;
      buf->extra_info->input_time = this->pts_counter / 90;
      buf->pts = this->pts_counter;

      if (remaining_sample_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      debug_idcin("    sending audio buf with %d bytes, %lld pts\n",
          buf->size, buf->pts);
      this->audio_fifo->put(this->audio_fifo, buf);
    }
  }

  debug_idcin("\n");

  this->pts_counter += IDCIN_FRAME_PTS_INC;

  return this->status;
}

/* returns 1 if the CIN file was opened successfully, 0 otherwise */
static int open_idcin_file(demux_idcin_t *this) {

  unsigned char header[IDCIN_HEADER_SIZE];
  unsigned char preview[MAX_PREVIEW_SIZE];

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {
    this->input->seek(this->input, 0, SEEK_SET);
    if (this->input->read(this->input, header, IDCIN_HEADER_SIZE) != 
      IDCIN_HEADER_SIZE)
      return 0;
  } else {
    this->input->get_optional_data(this->input, preview, 
      INPUT_OPTIONAL_DATA_PREVIEW);

    /* copy over the header bytes for processing */
    memcpy(header, preview, IDCIN_HEADER_SIZE);
  }

  /*
   * This is what you could call a "probabilistic" file check: Id CIN
   * files don't have a definite file signature. In lieu of such a marker,
   * perform sanity checks on the 5 header fields:
   *  width, height: greater than 0, less than or equal to 1024
   * audio sample rate: greater than or equal to 8000, less than or
   *  equal to 48000, or 0 for no audio
   * audio sample width (bytes/sample): 0 for no audio, or 1 or 2
   * audio channels: 0 for no audio, or 1 or 2
   */

  /* check the width */
  this->video_width = LE_32(&header[0]);
  if ((this->video_width == 0) || (this->video_width > 1024))
    return 0;

  /* check the height */
  this->video_height = LE_32(&header[4]);
  if ((this->video_height == 0) || (this->video_height > 1024))
    return 0;

  /* check the audio sample rate */
  this->audio_sample_rate = LE_32(&header[8]);
  if ((this->audio_sample_rate != 0) && 
     ((this->audio_sample_rate < 8000) || (this->audio_sample_rate > 48000)))
    return 0;

  /* check the audio bytes/sample */
  this->audio_bytes_per_sample = LE_32(&header[12]);
  if (this->audio_bytes_per_sample > 2)
    return 0;

  /* check the audio channels */
  this->audio_channels = LE_32(&header[16]);
  if (this->audio_channels > 2)
    return 0;

  /* if execution got this far, qualify it as a valid Id CIN file 
   * and continue loading */
  debug_idcin("  demux_idcin: %dx%d video, %d Hz, %d channels, %d bits PCM audio\n",
    this->video_width, this->video_height,
    this->audio_sample_rate, 
    this->audio_channels,
    this->audio_bytes_per_sample * 8);

  /* file is qualified; if the input was not seekable, read the header
   * bytes out of the stream */
  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) == 0) {
    if (this->input->read(this->input, header, IDCIN_HEADER_SIZE) != 
      IDCIN_HEADER_SIZE)
      return 0;
  }

  /* read the Huffman table */
  if (this->input->read(this->input, this->huffman_table,
    HUFFMAN_TABLE_SIZE) != HUFFMAN_TABLE_SIZE)
    return 0;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 
    (this->audio_channels) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] =
    this->video_width;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = 
    this->video_height;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->audio_channels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->audio_sample_rate;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->audio_bytes_per_sample * 8;

  return 1;
}

static void demux_idcin_send_headers(demux_plugin_t *this_gen) {

  demux_idcin_t *this = (demux_idcin_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = IDCIN_FRAME_PTS_INC;  /* initial video_step */
  /* really be a rebel: No structure at all, just put the video width
   * and height straight into the buffer, BE_16 format */
  buf->content[0] = (this->video_width >> 8) & 0xFF;
  buf->content[1] = (this->video_width >> 0) & 0xFF;
  buf->content[2] = (this->video_height >> 8) & 0xFF;
  buf->content[3] = (this->video_height >> 0) & 0xFF;
  buf->size = 4;
  buf->type = BUF_VIDEO_IDCIN;
  this->video_fifo->put (this->video_fifo, buf);

  /* send the Huffman table */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_SPECIAL;
  buf->decoder_info[1] = BUF_SPECIAL_IDCIN_HUFFMAN_TABLE;
  buf->decoder_info[2] = sizeof(this->huffman_table);
  buf->decoder_info_ptr[2] = &this->huffman_table;
  buf->size = 0;
  buf->type = BUF_VIDEO_IDCIN;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo && this->audio_channels) {

    /* initialize the chunk sizes */
    if (this->audio_sample_rate % 14 != 0) {
      this->audio_chunk_size1 = (this->audio_sample_rate / 14) *
        this->audio_bytes_per_sample * this->audio_channels;
      this->audio_chunk_size2 = (this->audio_sample_rate / 14 + 1) *
        this->audio_bytes_per_sample * this->audio_channels;
    } else {
      this->audio_chunk_size1 = this->audio_chunk_size2 =
        (this->audio_sample_rate / 14) * this->audio_bytes_per_sample *
        this->audio_channels;
    }
    debug_idcin("  demux_idcin: audio_chunk_size[1,2] = %d, %d\n",
      this->audio_chunk_size1, this->audio_chunk_size2);

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_LPCM_LE;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->audio_sample_rate;
    buf->decoder_info[2] = this->audio_bytes_per_sample * 8;
    buf->decoder_info[3] = this->audio_channels;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_idcin_seek (demux_plugin_t *this_gen,
                              off_t start_pos, int start_time) {

  demux_idcin_t *this = (demux_idcin_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    /* send new pts */
    xine_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  
    /* reposition stream past the Huffman tables */
    this->input->seek(this->input, 0x14 + 0x10000, SEEK_SET);

    this->pts_counter = 0;
    this->current_audio_chunk = 1;
  }

  return this->status;
}

static void demux_idcin_dispose (demux_plugin_t *this) {

  free(this);
}

static int demux_idcin_get_status (demux_plugin_t *this_gen) {
  demux_idcin_t *this = (demux_idcin_t *) this_gen;

  return this->status;
}

static int demux_idcin_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static uint32_t demux_idcin_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_idcin_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_idcin_t  *this;

  this         = xine_xmalloc (sizeof (demux_idcin_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_idcin_send_headers;
  this->demux_plugin.send_chunk        = demux_idcin_send_chunk;
  this->demux_plugin.seek              = demux_idcin_seek;
  this->demux_plugin.dispose           = demux_idcin_dispose;
  this->demux_plugin.get_status        = demux_idcin_get_status;
  this->demux_plugin.get_stream_length = demux_idcin_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_idcin_get_capabilities;
  this->demux_plugin.get_optional_data = demux_idcin_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_idcin_file(this)) {
      free (this);
      return NULL;
    }

  break;

  case METHOD_BY_EXTENSION: {
    char *ending, *mrl;

    mrl = input->get_mrl (input);

    ending = strrchr(mrl, '.');

    if (!ending) {
      free (this);
      return NULL;
    }

    if (strncasecmp (ending, ".cin", 4)) {
      free (this);
      return NULL;
    }

    if (!open_idcin_file(this)) {
      free (this);
      return NULL;
    }

  }

  break;

  default:
    free (this);
    return NULL;
  }

  strncpy (this->last_mrl, input->get_mrl (input), 1024);

  return &this->demux_plugin;
}


static char *get_description (demux_class_t *this_gen) {
  return "Id Quake II Cinematic file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "Id CIN";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "cin";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_idcin_class_t *this = (demux_idcin_class_t *) this_gen;

  free (this);
}

void *demux_idcin_init_plugin (xine_t *xine, void *data) {

  demux_idcin_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_idcin_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

#if 0
plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 21, "idcin", XINE_VERSION_CODE, NULL, demux_idcin_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
#endif

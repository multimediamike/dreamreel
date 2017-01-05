/*
 * Copyright (C) 2000-2002 the xine project
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
 * FILM (CPK) File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information on the FILM file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_film.c,v 1.59 2003/02/22 15:00:43 esnel Exp $
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

#define FOURCC_TAG( ch0, ch1, ch2, ch3 )                                \
        ( (long)(unsigned char)(ch3) | ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | ( (long)(unsigned char)(ch0) << 24 ) )

#define FILM_TAG FOURCC_TAG('F', 'I', 'L', 'M')
#define FDSC_TAG FOURCC_TAG('F', 'D', 'S', 'C')
#define STAB_TAG FOURCC_TAG('S', 'T', 'A', 'B')
#define CVID_TAG FOURCC_TAG('c', 'v', 'i', 'd')

typedef struct {
  int audio;  /* audio = 1, video = 0 */
  off_t sample_offset;
  unsigned int sample_size;
  int64_t pts;
  int64_t duration;
  int keyframe;
} film_sample_t;

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  off_t                data_start;
  off_t                data_size;
  int                  status;

  /* when this flag is set, demuxer only dispatches audio samples until it
   * encounters a video keyframe, then it starts sending every frame again */
  int                  waiting_for_keyframe;

  char                 version[4];

  /* video information */
  unsigned int         video_codec;
  unsigned int         video_type;
  xine_bmiheader       bih;

  /* audio information */
  unsigned int         audio_type;
  unsigned int         sample_rate;
  unsigned int         audio_bits;
  unsigned int         audio_channels;
  unsigned char       *interleave_buffer;

  /* playback information */
  unsigned int         frequency;
  unsigned int         sample_count;
  film_sample_t       *sample_table;
  unsigned int         current_sample;
  unsigned int         last_sample;
  int                  total_time;

  char                 last_mrl[1024];
} demux_film_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_film_class_t;

/* set DEBUG_FILM_LOAD to dump the frame index after the demuxer loads a
 * FILM file */
#define DEBUG_FILM_LOAD 0

/* set DEBUG_FILM_DEMUX to output information about the A/V chunks that the
 * demuxer is dispatching to the engine */
#define DEBUG_FILM_DEMUX 0

#if DEBUG_FILM_LOAD
#define debug_film_load printf
#else
static inline void debug_film_load(const char *format, ...) { }
#endif

#if DEBUG_FILM_DEMUX
#define debug_film_demux printf
#else
static inline void debug_film_demux(const char *format, ...) { }
#endif

/* Open a FILM file
 * This function is called from the _open() function of this demuxer.
 * It returns 1 if FILM file was opened successfully. */
static int open_film_file(demux_film_t *film) {

  unsigned char *film_header;
  unsigned int film_header_size;
  unsigned char scratch[16];
  unsigned char preview[MAX_PREVIEW_SIZE];
  unsigned int chunk_type;
  unsigned int chunk_size;
  unsigned int i, j;
  unsigned int audio_byte_count = 0;
  int64_t largest_pts = 0;
  unsigned int pts;

  /* initialize structure fields */
  film->bih.biWidth = 0;
  film->bih.biHeight = 0;
  film->video_codec = 0;
  film->sample_rate = 0;
  film->audio_bits = 0;
  film->audio_channels = 0;

  if (film->input->get_capabilities(film->input) & INPUT_CAP_SEEKABLE) {
    /* reset the file */
    film->input->seek(film->input, 0, SEEK_SET);

    /* get the signature, header length and file version */
    if (film->input->read(film->input, scratch, 16) != 16) {
      return 0;
    }
  } else {
    film->input->get_optional_data(film->input, preview,
      INPUT_OPTIONAL_DATA_PREVIEW);

    /* copy over the header bytes for processing */
    memcpy(scratch, preview, 16);
  }

  /* FILM signature correct? */
  if (strncmp(scratch, "FILM", 4)) {
    return 0;
  }
  debug_film_load("  demux_film: found 'FILM' signature\n");

  /* file is qualified; if the input was not seekable, skip over the header
   * bytes in the stream */
  if ((film->input->get_capabilities(film->input) & INPUT_CAP_SEEKABLE) == 0) {
    film->input->seek(film->input, 16, SEEK_SET);
  }

  /* header size = header size - 16-byte FILM signature */
  film_header_size = BE_32(&scratch[4]) - 16;
  film_header = xine_xmalloc(film_header_size);
  if (!film_header)
    return 0;
  strncpy(film->version, &scratch[8], 4);
  debug_film_load("  demux_film: 0x%X header bytes, version %c%c%c%c\n",
    film_header_size,
    film->version[0],
    film->version[1],
    film->version[2],
    film->version[3]);

  /* load the rest of the FILM header */
  if (film->input->read(film->input, film_header, film_header_size) != 
    film_header_size) {
    free (film->interleave_buffer);
    free (film->sample_table);
    free (film_header);
    return 0;
  }

  /* get the starting offset */
  film->data_start = film->input->get_current_pos(film->input);
  film->data_size = film->input->get_length(film->input) - film->data_start;

  /* traverse the FILM header */
  i = 0;
  while (i < film_header_size) {
    chunk_type = BE_32(&film_header[i]);
    chunk_size = BE_32(&film_header[i + 4]);

    /* sanity check the chunk size */
    if (i + chunk_size > film_header_size) {
      xine_log(film->stream->xine, XINE_LOG_MSG,
        _("invalid FILM chunk size\n"));
      free (film->interleave_buffer);
      free (film->sample_table);
      free (film_header);
      return 0;
    }

    switch(chunk_type) {
    case FDSC_TAG:
      debug_film_load("  demux_film: parsing FDSC chunk\n");

      /* always fetch the video information */
      film->bih.biWidth = BE_32(&film_header[i + 16]);
      film->bih.biHeight = BE_32(&film_header[i + 12]);
      film->video_codec = *(uint32_t *)&film_header[i + 8];
      film->video_type = fourcc_to_buf_video(*(uint32_t *)&film_header[i + 8]);

      if( !film->video_type )
        film->video_type = BUF_VIDEO_UNKNOWN;
      
      /* fetch the audio information if the chunk size checks out */
      if (chunk_size == 32) {
        film->audio_channels = film_header[21];
        film->audio_bits = film_header[22];
        film->sample_rate = BE_16(&film_header[24]);
      } else {
        /* If the FDSC chunk is not 32 bytes long, this is an early FILM
         * file. Make a few assumptions about the audio parms based on the
         * video codec used in the file. */
        if (film->video_type == BUF_VIDEO_CINEPAK) {
          film->audio_channels = 1;
          film->audio_bits = 8;
          film->sample_rate = 22050;
        } else if (film->video_type == BUF_VIDEO_SEGA) {
          film->audio_channels = 1;
          film->audio_bits = 8;
          film->sample_rate = 16000;
        }
      }
      if (film->sample_rate)
        film->audio_type = BUF_AUDIO_LPCM_BE;
      else
        film->audio_type = 0;

      if (film->video_type)
        debug_film_load("    video: %dx%d %c%c%c%c\n",
          film->bih.biWidth, film->bih.biHeight,
          film_header[i + 8],
          film_header[i + 9],
          film_header[i + 10],
          film_header[i + 11]);
      else
        debug_film_load("    no video\n");

      if (film->audio_type)
        debug_film_load("    audio: %d Hz, %d channels, %d bits PCM\n",
          film->sample_rate,
          film->audio_channels,
          film->audio_bits);
      else
        debug_film_load("    no audio\n");

      break;

    case STAB_TAG:
      debug_film_load("  demux_film: parsing STAB chunk\n");

      /* load the sample table */
      if (film->sample_table)
        free(film->sample_table);
      film->frequency = BE_32(&film_header[i + 8]);
      film->sample_count = BE_32(&film_header[i + 12]);
      film->sample_table =
        xine_xmalloc(film->sample_count * sizeof(film_sample_t));
      for (j = 0; j < film->sample_count; j++) {

        film->sample_table[j].sample_offset = 
          BE_32(&film_header[(i + 16) + j * 16 + 0])
          + film_header_size + 16;
        film->sample_table[j].sample_size = 
          BE_32(&film_header[(i + 16) + j * 16 + 4]);
        pts = 
          BE_32(&film_header[(i + 16) + j * 16 + 8]);
        film->sample_table[j].duration = 
          BE_32(&film_header[(i + 16) + j * 16 + 12]);

        if (pts == 0xFFFFFFFF) {

          film->sample_table[j].audio = 1;
          film->sample_table[j].keyframe = 0;

          /* figure out audio pts */
          film->sample_table[j].pts = audio_byte_count;
          film->sample_table[j].pts *= 90000;
          film->sample_table[j].pts /= 
            (film->sample_rate * film->audio_channels * (film->audio_bits / 8));
          audio_byte_count += film->sample_table[j].sample_size;

        } else {

          /* figure out video pts, duration, and keyframe */
          film->sample_table[j].audio = 0;

          /* keyframe if top bit of this field is 0 */
          if (pts & 0x80000000)
            film->sample_table[j].keyframe = 0;
          else
            film->sample_table[j].keyframe = 1;
 
          /* remove the keyframe bit */
          film->sample_table[j].pts = pts & 0x7FFFFFFF;

          /* compute the pts */
          film->sample_table[j].pts *= 90000;
          film->sample_table[j].pts /= film->frequency;

          /* compute the frame duration */
          film->sample_table[j].duration *= 90000;
          film->sample_table[j].duration /= film->frequency;

        }

        /* use this to calculate the total running time of the file */
        if (film->sample_table[j].pts > largest_pts)
          largest_pts = film->sample_table[j].pts;

        debug_film_load("    sample %4d @ %8llX, %8X bytes, %s, pts %lld, duration %lld%s\n",
          j,
          film->sample_table[j].sample_offset,
          film->sample_table[j].sample_size,
          (film->sample_table[j].audio) ? "audio" : "video",
          film->sample_table[j].pts,
          film->sample_table[j].duration,
          (film->sample_table[j].keyframe) ? " (keyframe)" : "");
      }

      /*
       * in some files, this chunk length does not account for the 16-byte
       * chunk preamble; watch for it
       */
      if (chunk_size == film->sample_count * 16)
        i += 16;

      /* allocate enough space in the interleave preload buffer for the 
       * first chunk (which will be more than enough for successive chunks) */
      if (film->audio_type) {
        if (film->interleave_buffer)
          free(film->interleave_buffer);
        film->interleave_buffer = 
          xine_xmalloc(film->sample_table[0].sample_size);
      }
      break;

    default:
      xine_log(film->stream->xine, XINE_LOG_MSG,
        _("unrecognized FILM chunk\n"));
      free (film->interleave_buffer);
      free (film->sample_table);
      free (film_header);
      return 0;
    }

    i += chunk_size;
  }

  film->total_time = largest_pts / 90;

  free (film_header);

  return 1;
}

static int demux_film_send_chunk(demux_plugin_t *this_gen) {

  demux_film_t *this = (demux_film_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int cvid_chunk_size;
  unsigned int i, j, k;
  int fixed_cvid_header;
  unsigned int remaining_sample_bytes;
  int first_buf;
  int interleave_index;

  i = this->current_sample;

  /* if there is an incongruency between last and current sample, it
   * must be time to send a new pts */
  if (this->last_sample + 1 != this->current_sample) {
    /* send new pts */
    xine_demux_control_newpts(this->stream, this->sample_table[i].pts,
      (this->sample_table[i].pts) ? BUF_FLAG_SEEK : 0);
  }

  this->last_sample = this->current_sample;
  this->current_sample++;

  /* check if all the samples have been sent */
  if (i >= this->sample_count) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* check if we're only sending audio samples until the next keyframe */
  if ((this->waiting_for_keyframe) && 
      (!this->sample_table[i].audio)) {
    if (this->sample_table[i].keyframe) {
      this->waiting_for_keyframe = 0;
    } else {
      /* move on to the next sample */
      return this->status;
    }
  }

  debug_film_demux("  demux_film: dispatching frame...\n");

  if ((!this->sample_table[i].audio) &&
    (this->video_type == BUF_VIDEO_CINEPAK)) {
    /* do a special song and dance when loading CVID data */
    if (this->version[0])
      cvid_chunk_size = this->sample_table[i].sample_size - 2;
    else
      cvid_chunk_size = this->sample_table[i].sample_size - 6;

    /* reset flag */
    fixed_cvid_header = 0;

    remaining_sample_bytes = cvid_chunk_size;
    this->input->seek(this->input, this->sample_table[i].sample_offset,
      SEEK_SET);

    while (remaining_sample_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = this->video_type;
      buf->extra_info->input_pos = 
        this->sample_table[i].sample_offset - this->data_start;
      buf->extra_info->input_length = this->data_size;
      buf->extra_info->input_time = this->sample_table[i].pts / 90;
      buf->pts = this->sample_table[i].pts;

      /* set the frame duration */
      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = this->sample_table[i].duration;
            
      if (remaining_sample_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (!fixed_cvid_header) {
        if (this->input->read(this->input, buf->content, 10) != 10) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }

        /* skip over the extra non-spec CVID bytes */
        this->input->seek(this->input, 
          this->sample_table[i].sample_size - cvid_chunk_size, SEEK_CUR);

        /* load the rest of the chunk */
        if (this->input->read(this->input, buf->content + 10, 
          buf->size - 10) != buf->size - 10) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }

        /* adjust the length in the CVID data chunk */
        buf->content[1] = (cvid_chunk_size >> 16) & 0xFF;
        buf->content[2] = (cvid_chunk_size >>  8) & 0xFF;
        buf->content[3] = (cvid_chunk_size >>  0) & 0xFF;

        fixed_cvid_header = 1;
      } else {
        if (this->input->read(this->input, buf->content, buf->size) !=
          buf->size) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }
      }

      if (this->sample_table[i].keyframe)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;
      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      debug_film_demux("    sending video buf with %d bytes, %lld pts, %d duration\n",
        buf->size, buf->pts, buf->decoder_info[0]);
      this->video_fifo->put(this->video_fifo, buf);
    }

  } else if (!this->sample_table[i].audio) {

    /* load a non-cvid video chunk */
    remaining_sample_bytes = this->sample_table[i].sample_size;
    this->input->seek(this->input, this->sample_table[i].sample_offset,
      SEEK_SET);

    while (remaining_sample_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = this->video_type;
      buf->extra_info->input_pos = 
        this->sample_table[i].sample_offset - this->data_start;
      buf->extra_info->input_length = this->data_size;
      buf->extra_info->input_time = this->sample_table[i].pts / 90;
      buf->pts = this->sample_table[i].pts;

      /* set the frame duration */
      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = this->sample_table[i].duration;
            
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

      if (this->sample_table[i].keyframe)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;
      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      debug_film_demux("    sending video buf with %d bytes, %lld pts, %d duration\n",
        buf->size, buf->pts, buf->decoder_info[0]);
      this->video_fifo->put(this->video_fifo, buf);
    }
  } else if(this->audio_fifo && this->audio_channels == 1) {

    /* load a mono audio sample and packetize it */
    remaining_sample_bytes = this->sample_table[i].sample_size;
    this->input->seek(this->input, this->sample_table[i].sample_offset,
      SEEK_SET);

    first_buf = 1;
    while (remaining_sample_bytes) {

      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->audio_type;
      buf->extra_info->input_pos = 
        this->sample_table[i].sample_offset - this->data_start;
      buf->extra_info->input_length = this->data_size;

      /* special hack to accomodate linear PCM decoder: only the first
       * buffer gets the real pts */
      if (first_buf) {
        buf->pts = this->sample_table[i].pts;
        first_buf = 0;
      } else
        buf->pts = 0;
      buf->extra_info->input_time = buf->pts / 90;

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

      if (this->video_type == BUF_VIDEO_SEGA) {
        /* if the file uses the SEGA video codec, assume this is
         * sign/magnitude audio */
        for (j = 0; j < buf->size; j++)
          if (buf->content[j] < 0x80)
            buf->content[j] += 0x80;
          else
            buf->content[j] = -(buf->content[j] & 0x7F) + 0x80;
      } else if (this->audio_bits == 8) {
        /* convert 8-bit data from signed -> unsigned */
        for (j = 0; j < buf->size; j++)
          buf->content[j] += 0x80;
      }

      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      debug_film_demux("    sending mono audio buf with %d bytes, %lld pts, %d duration\n",
        buf->size, buf->pts, buf->decoder_info[0]);
      this->audio_fifo->put(this->audio_fifo, buf);

    }
  } else if(this->audio_fifo && this->audio_channels == 2) {

    /* load an entire stereo sample and interleave the channels */

    /* load the whole chunk into the buffer */
    if (this->input->read(this->input, this->interleave_buffer,
      this->sample_table[i].sample_size) != this->sample_table[i].sample_size) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    /* proceed to de-interleave into individual buffers */
    remaining_sample_bytes = this->sample_table[i].sample_size / 2;
    interleave_index = 0;
    first_buf = 1;
    while (remaining_sample_bytes) {

      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->audio_type;
      buf->extra_info->input_pos = 
        this->sample_table[i].sample_offset - this->data_start;
      buf->extra_info->input_length = this->data_size;

      /* special hack to accomodate linear PCM decoder: only the first
       * buffer gets the real pts */
      if (first_buf) {
        buf->pts = this->sample_table[i].pts;
        first_buf = 0;
      } else
        buf->pts = 0;
      buf->extra_info->input_time = buf->pts / 90;

      if (remaining_sample_bytes > buf->max_size / 2)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes * 2;
      remaining_sample_bytes -= buf->size / 2;

      if (this->audio_bits == 16) {
        for (j = 0, k = interleave_index; j < buf->size; j += 4, k += 2) {
          buf->content[j] =     this->interleave_buffer[k];
          buf->content[j + 1] = this->interleave_buffer[k + 1];
        }
        for (j = 2, 
             k = interleave_index + this->sample_table[i].sample_size / 2; 
             j < buf->size; j += 4, k += 2) {
          buf->content[j] =     this->interleave_buffer[k];
          buf->content[j + 1] = this->interleave_buffer[k + 1];
        }
        interleave_index += buf->size / 2;
      } else {
        for (j = 0, k = interleave_index; j < buf->size; j += 2, k += 1) {
          buf->content[j] = this->interleave_buffer[k];
        }
        for (j = 1, 
             k = interleave_index + this->sample_table[i].sample_size / 2; 
             j < buf->size; j += 2, k += 1) {
          buf->content[j] = this->interleave_buffer[k];
        }
        interleave_index += buf->size / 2;
      }

      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      debug_film_demux("    sending stereo audio buf with %d bytes, %lld pts, %d duration\n",
        buf->size, buf->pts, buf->decoder_info[0]);
      this->audio_fifo->put(this->audio_fifo, buf);
    }
  }
  
  return this->status;
}

static void demux_film_send_headers(demux_plugin_t *this_gen) {

  demux_film_t *this = (demux_film_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 
    (this->video_type) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 
    (this->audio_type) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]  = this->bih.biWidth;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->bih.biHeight;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_FOURCC] = this->video_codec;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->audio_channels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->sample_rate;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->audio_bits;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->video_type) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = 3000;  /* initial video_step */
    memcpy(buf->content, &this->bih, sizeof(this->bih));
    buf->size = sizeof(this->bih);
    buf->type = this->video_type;
    this->video_fifo->put (this->video_fifo, buf);
  }

  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_LPCM_BE;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->sample_rate;
    buf->decoder_info[2] = this->audio_bits;
    buf->decoder_info[3] = this->audio_channels;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_film_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time) {
  demux_film_t *this = (demux_film_t *) this_gen;
  int best_index;
  int left, middle, right;
  int found;
  int64_t keyframe_pts;

  this->waiting_for_keyframe = 1;
  this->status = DEMUX_OK;
  xine_demux_flush_engine(this->stream);

  if( !this->stream->demux_thread_running ) {
    this->waiting_for_keyframe = 0;
    this->last_sample = 0;
  }
    
  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) == 0)
    return this->status;

  /* perform a binary search on the sample table, testing the offset 
   * boundaries first */
  if (start_pos) {
    if (start_pos <= 0)
      best_index = 0;
    else if (start_pos >= this->data_size) {
      this->status = DEMUX_FINISHED;
      return this->status;
    } else {
      start_pos += this->data_start;
      left = 0;
      right = this->sample_count - 1;
      found = 0;

      while (!found) {
        middle = (left + right) / 2;
        if ((start_pos >= this->sample_table[middle].sample_offset) &&
            (start_pos <= this->sample_table[middle].sample_offset + 
             this->sample_table[middle].sample_size)) {
          found = 1;
        } else if (start_pos < this->sample_table[middle].sample_offset) {
          right = middle;
        } else {
          left = middle;
        }
      }

      best_index = middle;
    }
  } else {
    int64_t pts = 90000 * start_time;

    if (pts <= this->sample_table[0].pts)
      best_index = 0;
    else if (pts >= this->sample_table[this->sample_count - 1].pts) {
      this->status = DEMUX_FINISHED;
      return this->status;
    } else {
      left = 0;
      right = this->sample_count - 1;
      do {
        middle = (left + right + 1) / 2;
        if (pts < this->sample_table[middle].pts) {
          right = (middle - 1);
        } else {
          left = middle;
        }
      } while (left < right);

      best_index = left;
    }
  }

  /* search back in the table for the nearest keyframe */
  while (best_index) {
    if (this->sample_table[best_index].keyframe) {
      break;
    }
    best_index--;
  }

  /* not done yet; now that the nearest keyframe has been found, seek
   * back to the first audio frame that has a pts less than or equal to
   * that of the keyframe */
  keyframe_pts = this->sample_table[best_index].pts;
  while (best_index) {
    if ((this->sample_table[best_index].audio) &&
        (this->sample_table[best_index].pts < keyframe_pts)) {
      break;
    }
    best_index--;
  }

  this->current_sample = best_index;
  
  return this->status;
}

static void demux_film_dispose (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  if (this->sample_table)
    free(this->sample_table);
  free(this->interleave_buffer);
  free(this);
}

static int demux_film_get_status (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  return this->status;
}

static int demux_film_get_stream_length (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  return this->total_time;
}

static uint32_t demux_film_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_film_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_film_t    *this;

  this         = xine_xmalloc (sizeof (demux_film_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_film_send_headers;
  this->demux_plugin.send_chunk        = demux_film_send_chunk;
  this->demux_plugin.seek              = demux_film_seek;
  this->demux_plugin.dispose           = demux_film_dispose;
  this->demux_plugin.get_status        = demux_film_get_status;
  this->demux_plugin.get_stream_length = demux_film_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_film_get_capabilities;
  this->demux_plugin.get_optional_data = demux_film_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_film_file(this)) {
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

    if (strncasecmp (ending, ".cpk", 4) &&
        strncasecmp (ending, ".cak", 4) &&
        strncasecmp (ending, ".film", 5)) {
      free (this);
      return NULL;
    }

    if (!open_film_file(this)) {
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
  return "FILM (CPK) demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "FILM (CPK)";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "cpk cak film";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_film_class_t *this = (demux_film_class_t *) this_gen;

  free (this);
}

void *demux_film_init_plugin (xine_t *xine, void *data) {

  demux_film_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_film_class_t));
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

/*
 * Copyright (C) 2001-2003 the xine project
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
 * Quicktime File Demuxer by Mike Melanson (melanson@pcisys.net)
 *  based on a Quicktime parsing experiment entitled 'lazyqt'
 *
 * Ideally, more documentation is forthcoming, but in the meantime:
 * functional flow:
 *  create_qt_info
 *  open_qt_file
 *   parse_moov_atom
 *    parse_mvhd_atom
 *    parse_trak_atom
 *    build_frame_table
 *  free_qt_info
 *
 * $Id: demux_qt.c,v 1.155 2003/04/07 21:28:33 guenter Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <zlib/zlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "buffer.h"
#include "bswap.h"

#include "qtpalette.h"

typedef unsigned int qt_atom;

#define QT_ATOM( ch0, ch1, ch2, ch3 ) \
        ( (unsigned char)(ch3) | \
        ( (unsigned char)(ch2) << 8 ) | \
        ( (unsigned char)(ch1) << 16 ) | \
        ( (unsigned char)(ch0) << 24 ) )

/* top level atoms */
#define FREE_ATOM QT_ATOM('f', 'r', 'e', 'e')
#define JUNK_ATOM QT_ATOM('j', 'u', 'n', 'k')
#define MDAT_ATOM QT_ATOM('m', 'd', 'a', 't')
#define MOOV_ATOM QT_ATOM('m', 'o', 'o', 'v')
#define PNOT_ATOM QT_ATOM('p', 'n', 'o', 't')
#define SKIP_ATOM QT_ATOM('s', 'k', 'i', 'p')
#define WIDE_ATOM QT_ATOM('w', 'i', 'd', 'e')
#define PICT_ATOM QT_ATOM('P', 'I', 'C', 'T')
#define FTYP_ATOM QT_ATOM('f', 't', 'y', 'p')

#define CMOV_ATOM QT_ATOM('c', 'm', 'o', 'v')

#define MVHD_ATOM QT_ATOM('m', 'v', 'h', 'd')

#define VMHD_ATOM QT_ATOM('v', 'm', 'h', 'd')
#define SMHD_ATOM QT_ATOM('s', 'm', 'h', 'd')

#define TRAK_ATOM QT_ATOM('t', 'r', 'a', 'k')
#define TKHD_ATOM QT_ATOM('t', 'k', 'h', 'd')
#define MDHD_ATOM QT_ATOM('m', 'd', 'h', 'd')
#define ELST_ATOM QT_ATOM('e', 'l', 's', 't')

/* atoms in a sample table */
#define STSD_ATOM QT_ATOM('s', 't', 's', 'd')
#define STSZ_ATOM QT_ATOM('s', 't', 's', 'z')
#define STSC_ATOM QT_ATOM('s', 't', 's', 'c')
#define STCO_ATOM QT_ATOM('s', 't', 'c', 'o')
#define STTS_ATOM QT_ATOM('s', 't', 't', 's')
#define STSS_ATOM QT_ATOM('s', 't', 's', 's')
#define CO64_ATOM QT_ATOM('c', 'o', '6', '4')

#define ESDS_ATOM QT_ATOM('e', 's', 'd', 's')
#define WAVE_ATOM QT_ATOM('w', 'a', 'v', 'e')

#define IMA4_FOURCC QT_ATOM('i', 'm', 'a', '4')
#define MP4A_FOURCC QT_ATOM('m', 'p', '4', 'a')
#define TWOS_FOURCC QT_ATOM('t', 'w', 'o', 's')
#define SOWT_FOURCC QT_ATOM('s', 'o', 'w', 't')
#define RAW_FOURCC  QT_ATOM('r', 'a', 'w', ' ')

#define UDTA_ATOM QT_ATOM('u', 'd', 't', 'a')
#define CPY_ATOM QT_ATOM(0xA9, 'c', 'p', 'y')
#define DES_ATOM QT_ATOM(0xA9, 'd', 'e', 's')
#define CMT_ATOM QT_ATOM(0xA9, 'c', 'm', 't')

#define RMDA_ATOM QT_ATOM('r', 'm', 'd', 'a')
#define RDRF_ATOM QT_ATOM('r', 'd', 'r', 'f')
#define RMDR_ATOM QT_ATOM('r', 'm', 'd', 'r')
#define RMVC_ATOM QT_ATOM('r', 'm', 'v', 'c')
#define QTIM_ATOM QT_ATOM('q', 't', 'i', 'm')

/* placeholder for cutting and pasting */
#define _ATOM QT_ATOM('', '', '', '')

#define ATOM_PREAMBLE_SIZE 8
#define PALETTE_COUNT 256

#define MAX_PTS_DIFF 100000

/* network bandwidth, cribbed from src/input/input_mms.c */
const int64_t bandwidths[]={14400,19200,28800,33600,34430,57600,
                            115200,262200,393216,524300,1544000,10485800};

/* these are things that can go wrong */
typedef enum {
  QT_OK,
  QT_FILE_READ_ERROR,
  QT_NO_MEMORY,
  QT_NOT_A_VALID_FILE,
  QT_NO_MOOV_ATOM,
  QT_NO_ZLIB,
  QT_ZLIB_ERROR,
  QT_HEADER_TROUBLE
} qt_error;

/* there are other types but these are the ones we usually care about */
typedef enum {

  MEDIA_AUDIO,
  MEDIA_VIDEO,
  MEDIA_OTHER

} media_type;

typedef struct {
  int64_t offset;
  unsigned int size;
  int64_t pts;
  int keyframe;
  unsigned int media_id;
} qt_frame;

typedef struct {
  unsigned int track_duration;
  unsigned int media_time;
} edit_list_table_t;

typedef struct {
  unsigned int first_chunk;
  unsigned int samples_per_chunk;
  unsigned int media_id;
} sample_to_chunk_table_t;

typedef struct {
  unsigned int count;
  unsigned int duration;
} time_to_sample_table_t;

typedef struct {
  unsigned char *url;
  int64_t data_rate;
  int qtim_version;
} reference_t;

typedef union {

  struct {
    /* the media id that corresponds to this trak */
    unsigned int media_id;

    /* offset into the stsd atom of the properties atom */
    unsigned int properties_offset;

    unsigned int codec_fourcc;
    unsigned int codec_buftype;
    unsigned int width;
    unsigned int height;
    int palette_count;
    palette_entry_t palette[PALETTE_COUNT];
    int depth;
    int edit_list_compensation;  /* special trick for edit lists */
  } video;

  struct {
    /* the media id that corresponds to this trak */
    unsigned int media_id;

    /* offset into the stsd atom of the properties atom */
    unsigned int properties_offset;

    unsigned int codec_fourcc;
    unsigned int codec_buftype;
    unsigned int sample_rate;
    unsigned int channels;
    unsigned int bits;
    unsigned int vbr;
    unsigned int wave_present;
    xine_waveformatex wave;

    /* special audio parameters */
    unsigned int samples_per_packet;
    unsigned int bytes_per_packet;
    unsigned int bytes_per_frame;
    unsigned int bytes_per_sample;
    unsigned int samples_per_frame;
  } audio;

} properties_t;

typedef struct {

  /* trak description */
  media_type type;

  /* one or more properties atoms for this trak */
  properties_t *stsd_atoms;
  int stsd_atoms_count;

  /* this is the current properties atom in use */
  properties_t *properties;

  /* internal frame table corresponding to this trak */
  qt_frame *frames;
  unsigned int frame_count;
  unsigned int current_frame;

  /* trak timescale */
  unsigned int timescale;

  /* flags that indicate how a trak is supposed to be used */
  unsigned int flags;
  
  /* decoder data pass information to the AAC decoder */
  void *decoder_config;
  int decoder_config_len;

  /* verbatim copy of the stsd atom */
  int             stsd_size;
  unsigned char  *stsd;

  /****************************************/
  /* temporary tables for loading a chunk */

  /* edit list table */
  unsigned int edit_list_count;
  edit_list_table_t *edit_list_table;

  /* chunk offsets */
  unsigned int chunk_offset_count;
  int64_t *chunk_offset_table;

  /* sample sizes */
  unsigned int sample_size;
  unsigned int sample_size_count;
  unsigned int *sample_size_table;

  /* sync samples, a.k.a., keyframes */
  unsigned int sync_sample_count;
  unsigned int *sync_sample_table;

  /* sample to chunk table */
  unsigned int sample_to_chunk_count;
  sample_to_chunk_table_t *sample_to_chunk_table;

  /* time to sample table */
  unsigned int time_to_sample_count;
  time_to_sample_table_t *time_to_sample_table;

} qt_trak;

typedef struct {
  int compressed_header;  /* 1 if there was a compressed moov; just FYI */

  unsigned int creation_time;  /* in ms since Jan-01-1904 */
  unsigned int modification_time;
  unsigned int timescale;  /* base clock frequency is Hz */
  unsigned int duration;
        
  int64_t moov_first_offset;

  int               trak_count;
  qt_trak          *traks;

  /* the trak numbers that won their respective frame count competitions */
  int               video_trak;
  int               audio_trak;
  int seek_flag;  /* this is set to indicate that a seek has just occurred */

  char              *copyright;
  char              *description;
  char              *comment;

  /* a QT movie may contain a number of references pointing to URLs */
  reference_t       *references;
  int                reference_count;
  int                chosen_reference;

  /* need to know base MRL to construct URLs from relative paths */
  char              *base_mrl;

  qt_error last_error;
} qt_info;

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  status;

  qt_info             *qt;
  xine_bmiheader       bih;
  unsigned int         current_frame;
  unsigned int         last_frame;

  off_t                data_start;
  off_t                data_size;

  int64_t              bandwidth;

  char                 last_mrl[1024];
} demux_qt_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_qt_class_t;

/**********************************************************************
 * lazyqt special debugging functions
 **********************************************************************/

/* define DEBUG_ATOM_LOAD as 1 to get a verbose parsing of the relevant 
 * atoms */
#define DEBUG_ATOM_LOAD 0

/* define DEBUG_EDIT_LIST as 1 to get a detailed look at how the demuxer is
 * handling edit lists */
#define DEBUG_EDIT_LIST 0

/* define DEBUG_FRAME_TABLE as 1 to dump the complete frame table that the
 * demuxer plans to use during file playback */
#define DEBUG_FRAME_TABLE 0

/* define DEBUG_VIDEO_DEMUX as 1 to see details about the video chunks the
 * demuxer is sending off to the video decoder */
#define DEBUG_VIDEO_DEMUX 0

/* define DEBUG_AUDIO_DEMUX as 1 to see details about the audio chunks the
 * demuxer is sending off to the audio decoder */
#define DEBUG_AUDIO_DEMUX 0

/* Define DEBUG_DUMP_MOOV as 1 to dump the raw moov atom to disk. This is
 * particularly useful in debugging a file with a compressed moov (cmov)
 * atom. The atom will be dumped to the filename specified as 
 * RAW_MOOV_FILENAME. */
#define DEBUG_DUMP_MOOV 0
#define RAW_MOOV_FILENAME "moovatom.raw"

#if DEBUG_ATOM_LOAD
#define debug_atom_load printf
#else
static inline void debug_atom_load(const char *format, ...) { }
#endif

#if DEBUG_EDIT_LIST
#define debug_edit_list printf
#else
static inline void debug_edit_list(const char *format, ...) { }
#endif

#if DEBUG_FRAME_TABLE
#define debug_frame_table printf
#else
static inline void debug_frame_table(const char *format, ...) { }
#endif

#if DEBUG_VIDEO_DEMUX
#define debug_video_demux printf
#else
static inline void debug_video_demux(const char *format, ...) { }
#endif

#if DEBUG_AUDIO_DEMUX
#define debug_audio_demux printf
#else
static inline void debug_audio_demux(const char *format, ...) { }
#endif

static void hexdump (char *buf, int length) {

  int i;

  printf ("demux_qt: ascii contents>");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    if ((c >= 32) && (c < 128))
      printf ("%c", c);
    else
      printf (".");
  }
  printf ("\n");

  printf ("demux_qt: complete hexdump of package follows:\ndemux_qt 0x0000:  ");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\ndemux_qt 0x%04x: ", i);

    if ((i % 2) == 1)
      printf (" ");

  }
  printf ("\n");
}

static inline void dump_moov_atom(unsigned char *moov_atom, int moov_atom_size) {
#if DEBUG_DUMP_MOOV

  FILE *f;

  f = fopen(RAW_MOOV_FILENAME, "w");
  if (!f) {
    perror(RAW_MOOV_FILENAME);
    return;
  }

  if (fwrite(moov_atom, moov_atom_size, 1, f) != 1)
    printf ("  qt debug: could not write moov atom to disk\n");

  fclose(f);

#endif
}

/**********************************************************************
 * lazyqt functions
 **********************************************************************/

/*
 * This function traverses a file and looks for a moov atom. Returns the
 * file offset of the beginning of the moov atom (that means the offset
 * of the 4-byte length preceding the characters 'moov'). Returns -1
 * if no moov atom was found.
 *
 * Note: Do not count on the input stream being positioned anywhere in
 * particular when this function is finished.
 */
static void find_moov_atom(input_plugin_t *input, off_t *moov_offset,
  int64_t *moov_size) {

  off_t atom_size;
  qt_atom atom;
  unsigned char atom_preamble[ATOM_PREAMBLE_SIZE];

  /* init the passed variables */
  *moov_offset = *moov_size = -1;

  /* take it from the top */
  if (input->seek(input, 0, SEEK_SET) != 0)
    return;

  /* traverse through the input */
  while (*moov_offset == -1) {
    if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
      ATOM_PREAMBLE_SIZE)
      break;

    atom_size = BE_32(&atom_preamble[0]);
    atom = BE_32(&atom_preamble[4]);

    /* if the moov atom is found, log the position and break from the loop */
    if (atom == MOOV_ATOM) {
      *moov_offset = input->get_current_pos(input) - ATOM_PREAMBLE_SIZE;
      *moov_size = atom_size;
      break;
    }

    /* special case alert: 'free' atoms are known to contain 'cmov' atoms.
     * If this is a free atom, check for cmov immediately following.
     * QT Player can handle it, so xine should too. */
    if (atom == FREE_ATOM) {

      /* get the next atom preamble */
      if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
        ATOM_PREAMBLE_SIZE)
        break;

      /* if there is a cmov, qualify this free atom as the moov atom */
      if (BE_32(&atom_preamble[4]) == CMOV_ATOM) {
        /* pos = current pos minus 2 atom preambles */
        *moov_offset = input->get_current_pos(input) - ATOM_PREAMBLE_SIZE * 2;
        *moov_size = atom_size;
        break;
      } else {
        /* otherwise, rewind the stream */
        input->seek(input, -ATOM_PREAMBLE_SIZE, SEEK_CUR);
      }
    }

    /* if this atom is not the moov atom, make sure that it is at least one
     * of the other top-level QT atom */
    if ((atom != FREE_ATOM) &&
        (atom != JUNK_ATOM) &&
        (atom != MDAT_ATOM) &&
        (atom != PNOT_ATOM) &&
        (atom != SKIP_ATOM) &&
        (atom != WIDE_ATOM) &&
        (atom != PICT_ATOM) &&
        (atom != FTYP_ATOM))
      break;

    /* 64-bit length special case */
    if (atom_size == 1) {
      if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) !=
        ATOM_PREAMBLE_SIZE)
        break;

      atom_size = BE_32(&atom_preamble[0]);
      atom_size <<= 32;
      atom_size |= BE_32(&atom_preamble[4]);
      atom_size -= ATOM_PREAMBLE_SIZE * 2;
    } else
      atom_size -= ATOM_PREAMBLE_SIZE;

    input->seek(input, atom_size, SEEK_CUR);
  }

  /* reset to the start of the stream on the way out */
  input->seek(input, 0, SEEK_SET);
}

/* create a qt_info structure or return NULL if no memory */
qt_info *create_qt_info(void) {
  qt_info *info;

  info = (qt_info *)xine_xmalloc(sizeof(qt_info));

  if (!info)
    return NULL;

  info->compressed_header = 0;

  info->creation_time = 0;
  info->modification_time = 0;
  info->timescale = 0;
  info->duration = 0;

  info->trak_count = 0;
  info->traks = NULL;

  info->video_trak = -1;
  info->audio_trak = -1;

  info->copyright = NULL;
  info->description = NULL;
  info->comment = NULL;

  info->references = NULL;
  info->reference_count = 0;
  info->chosen_reference = -1;

  info->base_mrl = NULL;

  info->last_error = QT_OK;

  return info;
}

/* release a qt_info structure and associated data */
void free_qt_info(qt_info *info) {

  int i;

  if(info) {
    if(info->traks) {
      for (i = 0; i < info->trak_count; i++) {
        free(info->traks[i].frames);
        free(info->traks[i].edit_list_table);
        free(info->traks[i].chunk_offset_table);
        /* this pointer might have been set to -1 as a special case */
        if (info->traks[i].sample_size_table != (void *)-1)
          free(info->traks[i].sample_size_table);
        free(info->traks[i].sync_sample_table);
        free(info->traks[i].sample_to_chunk_table);
        free(info->traks[i].time_to_sample_table);
        free(info->traks[i].decoder_config);
        free(info->traks[i].stsd);
      }
      free(info->traks);
    }
    if(info->references) {
      for (i = 0; i < info->reference_count; i++)
        free(info->references[i].url);
      free(info->references);
    }
    free(info->base_mrl);
    free(info->copyright);
    free(info->description);
    free(info->comment);
    free(info);
    info = NULL;
  }
}

/* returns 1 if the file is determined to be a QT file, 0 otherwise */
static int is_qt_file(input_plugin_t *qt_file) {

  off_t moov_atom_offset = -1;
  int64_t moov_atom_size = -1;
  int i;
  unsigned char atom_preamble[ATOM_PREAMBLE_SIZE];
  unsigned char preview[MAX_PREVIEW_SIZE];
  int len;

  /* if the input is non-seekable, be much more stringent about qualifying
   * a QT file: In this case, the moov must be the first atom in the file */
  if ((qt_file->get_capabilities(qt_file) & INPUT_CAP_SEEKABLE) == 0) {
    len = qt_file->get_optional_data(qt_file, preview, INPUT_OPTIONAL_DATA_PREVIEW);
    if (BE_32(&preview[4]) == MOOV_ATOM)
      return 1;
    else
      return 0;
  }

  find_moov_atom(qt_file, &moov_atom_offset, &moov_atom_size);
  if (moov_atom_offset == -1) {
    return 0;
  } else {
    /* check that the next atom in the chunk contains alphanumeric
     * characters in the atom type field; if not, disqualify the file 
     * as a QT file */
    qt_file->seek(qt_file, moov_atom_offset + ATOM_PREAMBLE_SIZE, SEEK_SET);
    if (qt_file->read(qt_file, atom_preamble, ATOM_PREAMBLE_SIZE) !=
      ATOM_PREAMBLE_SIZE)
      return 0;

    for (i = 4; i < 8; i++)
      if (!isalnum(atom_preamble[i]))
        return 0;
    return 1;
  }
}

/* fetch interesting information from the movie header atom */
static void parse_mvhd_atom(qt_info *info, unsigned char *mvhd_atom) {

  info->creation_time = BE_32(&mvhd_atom[0x0C]);
  info->modification_time = BE_32(&mvhd_atom[0x10]);
  info->timescale = BE_32(&mvhd_atom[0x14]);
  info->duration = BE_32(&mvhd_atom[0x18]);

  debug_atom_load("  qt: timescale = %d, duration = %d (%d seconds)\n",
    info->timescale, info->duration,
    info->duration / info->timescale);
}

/* helper function from mplayer's parse_mp4.c */
static int mp4_read_descr_len(unsigned char *s, uint32_t *length) {
  uint8_t b;
  uint8_t numBytes = 0;
  
  *length = 0;

  do {
    b = *s++;
    numBytes++;
    *length = (*length << 7) | (b & 0x7F);
  } while ((b & 0x80) && numBytes < 4);

  return numBytes;
}

/*
 * This function traverses through a trak atom searching for the sample
 * table atoms, which it loads into an internal trak structure.
 */
static qt_error parse_trak_atom (qt_trak *trak,
				 unsigned char *trak_atom) {

  int i, j, k;
  unsigned int trak_atom_size = BE_32(&trak_atom[0]);
  qt_atom current_atom;
  unsigned int current_atom_size;
  unsigned int atom_pos;
  unsigned int properties_offset;
  unsigned int current_stsd_atom_size;
  qt_error last_error = QT_OK;

  /* for palette traversal */
  int color_depth;
  int color_flag;
  int color_start;
  int color_count;
  int color_end;
  int color_index;
  int color_dec;
  int color_greyscale;
  unsigned char *color_table;

  /* initialize trak structure */
  trak->edit_list_count = 0;
  trak->edit_list_table = NULL;
  trak->chunk_offset_count = 0;
  trak->chunk_offset_table = NULL;
  trak->sample_size = 0;
  trak->sample_size_count = 0;
  trak->sample_size_table = NULL;
  trak->sync_sample_table = 0;
  trak->sync_sample_table = NULL;
  trak->sample_to_chunk_count = 0;
  trak->sample_to_chunk_table = NULL;
  trak->time_to_sample_count = 0;
  trak->time_to_sample_table = NULL;
  trak->frames = NULL;
  trak->frame_count = 0;
  trak->current_frame = 0;
  trak->timescale = 0;
  trak->flags = 0;
  trak->decoder_config = NULL;
  trak->decoder_config_len = 0;
  trak->stsd = NULL;
  trak->stsd_size = 0;
  trak->stsd_atoms_count = 0;
  trak->stsd_atoms = NULL;

  /* default type */
  trak->type = MEDIA_OTHER;

  /* search for media type atoms */
  for (i = ATOM_PREAMBLE_SIZE; i < trak_atom_size - 4; i++) {
    current_atom = BE_32(&trak_atom[i]);

    if (current_atom == VMHD_ATOM) {
      trak->type = MEDIA_VIDEO;
      break;
    } else if (current_atom == SMHD_ATOM) {
      trak->type = MEDIA_AUDIO;
      break;
    }
  }
  
  debug_atom_load("  qt: parsing %s trak atom\n",
    (trak->type == MEDIA_VIDEO) ? "video" :
      (trak->type == MEDIA_AUDIO) ? "audio" : "other");

  /* search for the useful atoms */
  for (i = ATOM_PREAMBLE_SIZE; i < trak_atom_size - 4; i++) {
    current_atom_size = BE_32(&trak_atom[i - 4]);	
    current_atom = BE_32(&trak_atom[i]);

    if (current_atom == TKHD_ATOM) {
      trak->flags = BE_16(&trak_atom[i + 6]);
#if 0
      if (trak->type == MEDIA_VIDEO) {
        /* fetch display parameters */
        if( !trak->stsd_atoms[j].video.width ||
            !trak->stsd_atoms[j].video.height ) {

          trak->stsd_atoms[j].video.width =
            BE_16(&trak_atom[i + 0x50]);
          trak->stsd_atoms[j].video.height =
            BE_16(&trak_atom[i + 0x54]); 
        }
      }
#endif
    } else if (current_atom == ELST_ATOM) {

      /* there should only be one edit list table */
      if (trak->edit_list_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_trak;
      }

      trak->edit_list_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt elst atom (edit list atom): %d entries\n",
        trak->edit_list_count);

      trak->edit_list_table = (edit_list_table_t *)xine_xmalloc(
        trak->edit_list_count * sizeof(edit_list_table_t));
      if (!trak->edit_list_table) {
        last_error = QT_NO_MEMORY;
        goto free_trak;
      }

      /* load the edit list table */
      for (j = 0; j < trak->edit_list_count; j++) {
        trak->edit_list_table[j].track_duration =
          BE_32(&trak_atom[i + 12 + j * 12 + 0]);
        trak->edit_list_table[j].media_time =
          BE_32(&trak_atom[i + 12 + j * 12 + 4]);
        debug_atom_load("      %d: track duration = %d, media time = %d\n",
          j,
          trak->edit_list_table[j].track_duration,
          trak->edit_list_table[j].media_time);
      }

    } else if (current_atom == MDHD_ATOM)
      trak->timescale = BE_32(&trak_atom[i + 0x10]);
    else if (current_atom == STSD_ATOM) {

      debug_atom_load ("demux_qt: stsd atom\n");
#if DEBUG_ATOM_LOAD
      hexdump (&trak_atom[i], current_atom_size);
#endif

      /* copy whole stsd atom so it can later be sent to the decoder */
      trak->stsd_size = current_atom_size;
      trak->stsd = xine_xmalloc(current_atom_size);
      memset (trak->stsd, 0, current_atom_size);
      memcpy (trak->stsd, &trak_atom[i], current_atom_size);

      /* allocate space for each of the properties unions */
      trak->stsd_atoms_count = BE_32(&trak_atom[i + 8]);
      trak->stsd_atoms = xine_xmalloc(trak->stsd_atoms_count * sizeof(properties_t));
      if (!trak->stsd_atoms) {
        last_error = QT_NO_MEMORY;
        goto free_trak;
      }
      memset(trak->stsd_atoms, 0, trak->stsd_atoms_count * sizeof(properties_t));

      atom_pos = i + 0x10;
      properties_offset = 0x0C;
      for (k = 0; k < trak->stsd_atoms_count; k++) {

        current_stsd_atom_size = BE_32(&trak_atom[atom_pos - 4]);      
        if (trak->type == MEDIA_VIDEO) {

          trak->stsd_atoms[k].video.media_id = k + 1;
          trak->stsd_atoms[k].video.properties_offset = properties_offset;

          /* initialize to sane values */
          trak->stsd_atoms[k].video.width = 0;
          trak->stsd_atoms[k].video.height = 0;
          trak->stsd_atoms[k].video.depth = 0;

          /* assume no palette at first */
          trak->stsd_atoms[k].video.palette_count = 0;

          /* fetch video parameters */
          if( BE_16(&trak_atom[atom_pos + 0x1C]) && 
              BE_16(&trak_atom[atom_pos + 0x1E]) ) {
            trak->stsd_atoms[k].video.width =
              BE_16(&trak_atom[atom_pos + 0x1C]);
            trak->stsd_atoms[k].video.height =
              BE_16(&trak_atom[atom_pos + 0x1E]);
          }
          trak->stsd_atoms[k].video.codec_fourcc =
            ME_32(&trak_atom[atom_pos + 0x00]);

          /* figure out the palette situation */
          color_depth = trak_atom[atom_pos + 0x4F];
          trak->stsd_atoms[k].video.depth = color_depth;
          color_greyscale = color_depth & 0x20;
          color_depth &= 0x1F;

          /* if the depth is 2, 4, or 8 bpp, file is palettized */
          if ((color_depth == 2) || (color_depth == 4) || (color_depth == 8)) {

            color_flag = BE_16(&trak_atom[atom_pos + 0x50]);

            if (color_greyscale) {

              trak->stsd_atoms[k].video.palette_count =
                1 << color_depth;

              /* compute the greyscale palette */
              color_index = 255;
              color_dec = 256 / 
                (trak->stsd_atoms[k].video.palette_count - 1);
              for (j = 0; 
                   j < trak->stsd_atoms[k].video.palette_count;
                   j++) {

                trak->stsd_atoms[k].video.palette[j].r = color_index;
                trak->stsd_atoms[k].video.palette[j].g = color_index;
                trak->stsd_atoms[k].video.palette[j].b = color_index;
                color_index -= color_dec;
                if (color_index < 0)
                  color_index = 0;
              }

            } else if (color_flag & 0x08) {

              /* if flag bit 3 is set, load the default palette */
              trak->stsd_atoms[k].video.palette_count =
                1 << color_depth;

              if (color_depth == 2)
                color_table = qt_default_palette_4;
              else if (color_depth == 4)
                color_table = qt_default_palette_16;
              else
                color_table = qt_default_palette_256;

              for (j = 0; 
                j < trak->stsd_atoms[k].video.palette_count;
                j++) {

                trak->stsd_atoms[k].video.palette[j].r =
                  color_table[j * 4 + 0];
                trak->stsd_atoms[k].video.palette[j].g =
                  color_table[j * 4 + 1];
                trak->stsd_atoms[k].video.palette[j].b =
                  color_table[j * 4 + 2];

              }

            } else {

              /* load the palette from the file */
              color_start = BE_32(&trak_atom[atom_pos + 0x52]);
              color_count = BE_16(&trak_atom[atom_pos + 0x56]);
              color_end = BE_16(&trak_atom[atom_pos + 0x58]);
              trak->stsd_atoms[k].video.palette_count =
                color_end + 1;

              for (j = color_start; j <= color_end; j++) {

                color_index = BE_16(&trak_atom[atom_pos + 0x5A + j * 8]);
                if (color_count & 0x8000)
                  color_index = j;
                if (color_index < 
                  trak->stsd_atoms[k].video.palette_count) {
                  trak->stsd_atoms[k].video.palette[color_index].r =
                    trak_atom[atom_pos + 0x5A + j * 8 + 2];
                  trak->stsd_atoms[k].video.palette[color_index].g =
                    trak_atom[atom_pos + 0x5A + j * 8 + 4];
                  trak->stsd_atoms[k].video.palette[color_index].b =
                    trak_atom[atom_pos + 0x5A + j * 8 + 6];
                }
              }
            }
          } else
            trak->stsd_atoms[k].video.palette_count = 0;

          debug_atom_load("    video properties atom #%d\n", k + 1);
          debug_atom_load("      %dx%d, video fourcc = '%c%c%c%c' (%02X%02X%02X%02X)\n",
            trak->stsd_atoms[k].video.width,
            trak->stsd_atoms[k].video.height,
            trak_atom[atom_pos + 0x0],
            trak_atom[atom_pos + 0x1],
            trak_atom[atom_pos + 0x2],
            trak_atom[atom_pos + 0x3],
            trak_atom[atom_pos + 0x0],
            trak_atom[atom_pos + 0x1],
            trak_atom[atom_pos + 0x2],
            trak_atom[atom_pos + 0x3]);
          debug_atom_load("      %d RGB colors\n",
            trak->stsd_atoms[k].video.palette_count);
          for (j = 0; j < trak->stsd_atoms[k].video.palette_count;
               j++)
            debug_atom_load("        %d: %3d %3d %3d\n",
              j,
              trak->stsd_atoms[k].video.palette[j].r,
              trak->stsd_atoms[k].video.palette[j].g,
              trak->stsd_atoms[k].video.palette[j].b);

        } else if (trak->type == MEDIA_AUDIO) {

          trak->stsd_atoms[k].audio.media_id = k + 1;
          trak->stsd_atoms[k].audio.properties_offset = properties_offset;

          /* fetch audio parameters */
          trak->stsd_atoms[k].audio.codec_fourcc =
            ME_32(&trak_atom[atom_pos + 0x0]);
          trak->stsd_atoms[k].audio.sample_rate =
            BE_16(&trak_atom[atom_pos + 0x1C]);
          trak->stsd_atoms[k].audio.channels = trak_atom[atom_pos + 0x15];
          trak->stsd_atoms[k].audio.bits = trak_atom[atom_pos + 0x17];

          /* assume uncompressed audio parameters */
          trak->stsd_atoms[k].audio.bytes_per_sample =
            trak->stsd_atoms[k].audio.bits / 8;
          trak->stsd_atoms[k].audio.samples_per_frame =
            trak->stsd_atoms[k].audio.channels;
          trak->stsd_atoms[k].audio.bytes_per_frame = 
            trak->stsd_atoms[k].audio.bytes_per_sample * 
            trak->stsd_atoms[k].audio.samples_per_frame;
          trak->stsd_atoms[k].audio.samples_per_packet = 
            trak->stsd_atoms[k].audio.samples_per_frame;
          trak->stsd_atoms[k].audio.bytes_per_packet = 
            trak->stsd_atoms[k].audio.bytes_per_sample;

          /* special case time: some ima4-encoded files don't have the
           * extra header; compensate */
          if (BE_32(&trak_atom[atom_pos + 0x0]) == IMA4_FOURCC) {
            trak->stsd_atoms[k].audio.samples_per_packet = 64;
            trak->stsd_atoms[k].audio.bytes_per_packet = 34;
            trak->stsd_atoms[k].audio.bytes_per_frame = 34 * 
              trak->stsd_atoms[k].audio.channels;
            trak->stsd_atoms[k].audio.bytes_per_sample = 2;
            trak->stsd_atoms[k].audio.samples_per_frame = 64 *
              trak->stsd_atoms[k].audio.channels;
          }

          /* it's time to dig a little deeper to determine the real audio
           * properties; if a the stsd compressor atom has 0x24 bytes, it
           * appears to be a handler for uncompressed data; if there are an
           * extra 0x10 bytes, there are some more useful decoding params;
           * further, do not do load these parameters if the audio is just
           * PCM ('raw ', 'twos', or 'sowt') */
          if ((current_stsd_atom_size > 0x24) &&
              (trak->stsd_atoms[k].audio.codec_fourcc != TWOS_FOURCC) &&
              (trak->stsd_atoms[k].audio.codec_fourcc != SOWT_FOURCC) &&
              (trak->stsd_atoms[k].audio.codec_fourcc != RAW_FOURCC)) {

            if (BE_32(&trak_atom[atom_pos + 0x20]))
              trak->stsd_atoms[k].audio.samples_per_packet = 
                BE_32(&trak_atom[atom_pos + 0x20]);
            if (BE_32(&trak_atom[atom_pos + 0x24]))
              trak->stsd_atoms[k].audio.bytes_per_packet = 
                BE_32(&trak_atom[atom_pos + 0x24]);
            if (BE_32(&trak_atom[atom_pos + 0x28]))
              trak->stsd_atoms[k].audio.bytes_per_frame = 
                BE_32(&trak_atom[atom_pos + 0x28]);
            if (BE_32(&trak_atom[atom_pos + 0x2C]))
              trak->stsd_atoms[k].audio.bytes_per_sample = 
                BE_32(&trak_atom[atom_pos + 0x2C]);
            trak->stsd_atoms[k].audio.samples_per_frame =
              (trak->stsd_atoms[k].audio.bytes_per_frame / 
               trak->stsd_atoms[k].audio.bytes_per_packet) *
               trak->stsd_atoms[k].audio.samples_per_packet;
          }

          /* see if the trak deserves a promotion to VBR */
          if (BE_16(&trak_atom[atom_pos + 0x18]) == 0xFFFE)
            trak->stsd_atoms[k].audio.vbr = 1;
          else
            trak->stsd_atoms[k].audio.vbr = 0;

          /* if this is MP4 audio, mark the trak as VBR */
          if (BE_32(&trak_atom[atom_pos + 0x0]) == MP4A_FOURCC)
            trak->stsd_atoms[k].audio.vbr = 1;

          /* check for a MS-style WAVE format header */
          if ((current_atom_size >= 0x48) && 
              (BE_32(&trak_atom[atom_pos + 0x34]) == WAVE_ATOM)) {
            trak->stsd_atoms[k].audio.wave_present = 1;
            memcpy(&trak->stsd_atoms[k].audio.wave, 
              &trak_atom[atom_pos + 0x4C],
              sizeof(trak->stsd_atoms[k].audio.wave));
            xine_waveformatex_le2me(&trak->stsd_atoms[k].audio.wave);
          } else {
            trak->stsd_atoms[k].audio.wave_present = 0;
          }

          debug_atom_load("    audio properties atom #%d\n", k + 1);
          debug_atom_load("      %d Hz, %d bits, %d channels, %saudio fourcc = '%c%c%c%c' (%02X%02X%02X%02X)\n",
            trak->stsd_atoms[k].audio.sample_rate,
            trak->stsd_atoms[k].audio.bits,
            trak->stsd_atoms[k].audio.channels,
            (trak->stsd_atoms[k].audio.vbr) ? "vbr, " : "",
            trak_atom[atom_pos + 0x0],
            trak_atom[atom_pos + 0x1],
            trak_atom[atom_pos + 0x2],
            trak_atom[atom_pos + 0x3],
            trak_atom[atom_pos + 0x0],
            trak_atom[atom_pos + 0x1],
            trak_atom[atom_pos + 0x2],
            trak_atom[atom_pos + 0x3]);
          if (current_stsd_atom_size > 0x24) {
            debug_atom_load("      %d samples/packet, %d bytes/packet, %d bytes/frame\n",
              trak->stsd_atoms[k].audio.samples_per_packet,
              trak->stsd_atoms[k].audio.bytes_per_packet,
              trak->stsd_atoms[k].audio.bytes_per_frame);
            debug_atom_load("      %d bytes/sample (%d samples/frame)\n",
              trak->stsd_atoms[k].audio.bytes_per_sample,
              trak->stsd_atoms[k].audio.samples_per_frame);
          }
        }

        /* use first audio properties atom for now */
        trak->properties = &trak->stsd_atoms[0];

        /* forward to the next atom */
        atom_pos += current_stsd_atom_size;
        properties_offset += current_stsd_atom_size;
      }

    } else if (current_atom == ESDS_ATOM) {

      uint32_t len;
      
      debug_atom_load("    qt/mpeg-4 esds atom\n");

      if ((trak->type == MEDIA_VIDEO) || 
          (trak->type == MEDIA_AUDIO)) {
        
        j = i + 8;
        if( trak_atom[j++] == 0x03 ) {
          j += mp4_read_descr_len( &trak_atom[j], &len );
          j++;
        }
        j += 2;
        if( trak_atom[j++] == 0x04 ) {
          j += mp4_read_descr_len( &trak_atom[j], &len );
          j += 13;
          if( trak_atom[j++] == 0x05 ) {
            j += mp4_read_descr_len( &trak_atom[j], &len );
            debug_atom_load("      decoder config is %d (0x%X) bytes long\n",
              len, len);
            trak->decoder_config = realloc(trak->decoder_config, len);
            trak->decoder_config_len = len;
            memcpy(trak->decoder_config,&trak_atom[j],len);
          }
        }
      }

    } else if (current_atom == STSZ_ATOM) {

      /* there should only be one of these atoms */
      if (trak->sample_size_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_trak;
      }

      trak->sample_size = BE_32(&trak_atom[i + 8]);
      trak->sample_size_count = BE_32(&trak_atom[i + 12]);

      debug_atom_load("    qt stsz atom (sample size atom): sample size = %d, %d entries\n",
        trak->sample_size, trak->sample_size_count);

      /* allocate space and load table only if sample size is 0 */
      if (trak->sample_size == 0) {
        trak->sample_size_table = (unsigned int *)malloc(
          trak->sample_size_count * sizeof(unsigned int));
        if (!trak->sample_size_table) {
          last_error = QT_NO_MEMORY;
          goto free_trak;
        }
        /* load the sample size table */
        for (j = 0; j < trak->sample_size_count; j++) {
          trak->sample_size_table[j] =
            BE_32(&trak_atom[i + 16 + j * 4]);
          debug_atom_load("      sample size %d: %d\n",
            j, trak->sample_size_table[j]);
        }
      } else
        /* set the pointer to non-NULL to indicate that the atom type has
         * already been seen for this trak atom */
        trak->sample_size_table = (void *)-1;

    } else if (current_atom == STSS_ATOM) {

      /* there should only be one of these atoms */
      if (trak->sync_sample_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_trak;
      }

      trak->sync_sample_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt stss atom (sample sync atom): %d sync samples\n",
        trak->sync_sample_count);

      trak->sync_sample_table = (unsigned int *)malloc(
        trak->sync_sample_count * sizeof(unsigned int));
      if (!trak->sync_sample_table) {
        last_error = QT_NO_MEMORY;
        goto free_trak;
      }

      /* load the sync sample table */
      for (j = 0; j < trak->sync_sample_count; j++) {
        trak->sync_sample_table[j] =
          BE_32(&trak_atom[i + 12 + j * 4]);
        debug_atom_load("      sync sample %d: sample %d (%d) is a keyframe\n",
          j, trak->sync_sample_table[j],
          trak->sync_sample_table[j] - 1);
      }

    } else if (current_atom == STCO_ATOM) {

      /* there should only be one of either stco or co64 */
      if (trak->chunk_offset_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_trak;
      }

      trak->chunk_offset_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt stco atom (32-bit chunk offset atom): %d chunk offsets\n",
        trak->chunk_offset_count);

      trak->chunk_offset_table = (int64_t *)malloc(
        trak->chunk_offset_count * sizeof(int64_t));
      if (!trak->chunk_offset_table) {
        last_error = QT_NO_MEMORY;
        goto free_trak;
      }

      /* load the chunk offset table */
      for (j = 0; j < trak->chunk_offset_count; j++) {
        trak->chunk_offset_table[j] =
          BE_32(&trak_atom[i + 12 + j * 4]);
        debug_atom_load("      chunk %d @ 0x%llX\n",
          j, trak->chunk_offset_table[j]);
      }

    } else if (current_atom == CO64_ATOM) {

      /* there should only be one of either stco or co64 */
      if (trak->chunk_offset_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_trak;
      }

      trak->chunk_offset_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt co64 atom (64-bit chunk offset atom): %d chunk offsets\n",
        trak->chunk_offset_count);

      trak->chunk_offset_table = (int64_t *)malloc(
        trak->chunk_offset_count * sizeof(int64_t));
      if (!trak->chunk_offset_table) {
        last_error = QT_NO_MEMORY;
        goto free_trak;
      }

      /* load the 64-bit chunk offset table */
      for (j = 0; j < trak->chunk_offset_count; j++) {
        trak->chunk_offset_table[j] =
          BE_32(&trak_atom[i + 12 + j * 8 + 0]);
        trak->chunk_offset_table[j] <<= 32;
        trak->chunk_offset_table[j] |=
          BE_32(&trak_atom[i + 12 + j * 8 + 4]);
        debug_atom_load("      chunk %d @ 0x%llX\n",
          j, trak->chunk_offset_table[j]);
      }

    } else if (current_atom == STSC_ATOM) {

      /* there should only be one of these atoms */
      if (trak->sample_to_chunk_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_trak;
      }

      trak->sample_to_chunk_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt stsc atom (sample-to-chunk atom): %d entries\n",
        trak->sample_to_chunk_count);

      trak->sample_to_chunk_table = (sample_to_chunk_table_t *)malloc(
        trak->sample_to_chunk_count * sizeof(sample_to_chunk_table_t));
      if (!trak->sample_to_chunk_table) {
        last_error = QT_NO_MEMORY;
        goto free_trak;
      }

      /* load the sample to chunk table */
      for (j = 0; j < trak->sample_to_chunk_count; j++) {
        trak->sample_to_chunk_table[j].first_chunk =
          BE_32(&trak_atom[i + 12 + j * 12 + 0]);
        trak->sample_to_chunk_table[j].samples_per_chunk =
          BE_32(&trak_atom[i + 12 + j * 12 + 4]);
        trak->sample_to_chunk_table[j].media_id =
          BE_32(&trak_atom[i + 12 + j * 12 + 8]);
        debug_atom_load("      %d: %d samples/chunk starting at chunk %d (%d) for media id %d\n",
          j, trak->sample_to_chunk_table[j].samples_per_chunk,
          trak->sample_to_chunk_table[j].first_chunk,
          trak->sample_to_chunk_table[j].first_chunk - 1,
          trak->sample_to_chunk_table[j].media_id);
      }

    } else if (current_atom == STTS_ATOM) {

      /* there should only be one of these atoms */
      if (trak->time_to_sample_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_trak;
      }

      trak->time_to_sample_count = BE_32(&trak_atom[i + 8]);

      debug_atom_load("    qt stts atom (time-to-sample atom): %d entries\n",
        trak->time_to_sample_count);

      trak->time_to_sample_table = (time_to_sample_table_t *)malloc(
        (trak->time_to_sample_count+1) * sizeof(time_to_sample_table_t));
      if (!trak->time_to_sample_table) {
        last_error = QT_NO_MEMORY;
        goto free_trak;
      }

      /* load the time to sample table */
      for (j = 0; j < trak->time_to_sample_count; j++) {
        trak->time_to_sample_table[j].count =
          BE_32(&trak_atom[i + 12 + j * 8 + 0]);
        trak->time_to_sample_table[j].duration =
          BE_32(&trak_atom[i + 12 + j * 8 + 4]);
        debug_atom_load("      %d: count = %d, duration = %d\n",
          j, trak->time_to_sample_table[j].count,
          trak->time_to_sample_table[j].duration);
      }
      trak->time_to_sample_table[j].count = 0; /* terminate with zero */
    }
  }

  return QT_OK;

  /* jump here to make sure everything is free'd and avoid leaking memory */
free_trak:
  free(trak->edit_list_table);
  free(trak->chunk_offset_table);
  /* this pointer might have been set to -1 as a special case */
  if (trak->sample_size_table != (void *)-1)
    free(trak->sample_size_table);
  free(trak->sync_sample_table);
  free(trak->sample_to_chunk_table);
  free(trak->time_to_sample_table);
  free(trak->decoder_config);
  free(trak->stsd);
  free(trak->stsd_atoms);

  return last_error;
}

/* Traverse through a reference atom and extract the URL and data rate. */
static qt_error parse_reference_atom (reference_t *ref,
                                      unsigned char *ref_atom,
                                      char *base_mrl) {

  int i, j;
  unsigned int ref_atom_size = BE_32(&ref_atom[0]);
  qt_atom current_atom;
  unsigned int current_atom_size;

  /* initialize reference atom */
  ref->url = NULL;
  ref->data_rate = 0;
  ref->qtim_version = 0;

  /* traverse through the atom looking for the key atoms */
  for (i = ATOM_PREAMBLE_SIZE; i < ref_atom_size - 4; i++) {

    current_atom_size = BE_32(&ref_atom[i - 4]);
    current_atom = BE_32(&ref_atom[i]);

    if (current_atom == RDRF_ATOM) {

      /* if the URL starts with "http://", copy it */
      if (strncmp(&ref_atom[i + 12], "http://", 7) == 0) {

        /* URL is spec'd to terminate with a NULL; don't trust it */
        ref->url = xine_xmalloc(BE_32(&ref_atom[i + 12]) + 1);
        strncpy(ref->url, &ref_atom[i + 16], BE_32(&ref_atom[i + 12]));
        ref->url[BE_32(&ref_atom[i + 12]) - 1] = '\0';

      } else {

        int string_size = strlen(base_mrl) + BE_32(&ref_atom[i + 12]) + 1;

        /* otherwise, append relative URL to base MRL */
        ref->url = xine_xmalloc(string_size);
        strcpy(ref->url, base_mrl);
        strncat(ref->url, &ref_atom[i + 16], BE_32(&ref_atom[i + 12]));
        ref->url[string_size - 1] = '\0';
      }

      debug_atom_load("    qt rdrf URL reference:\n      %s\n", ref->url);

    } else if (current_atom == RMDR_ATOM) {

      /* load the data rate */
      ref->data_rate = BE_32(&ref_atom[i + 8]);
      ref->data_rate *= 10;

      debug_atom_load("    qt rmdr data rate = %lld\n", ref->data_rate);

    } else if (current_atom == RMVC_ATOM) {

      debug_atom_load("    qt rmvc atom\n");

      /* search the rmvc atom for 'qtim'; 2 bytes will follow the qtim
       * chars so only search to 6 bytes to the end */
      for (j = 4; j < current_atom_size - 6; j++) {

        if (BE_32(&ref_atom[i + j]) == QTIM_ATOM) {

          ref->qtim_version = BE_16(&ref_atom[i + j + 4]);
          debug_atom_load("      qtim version = %04X\n", ref->qtim_version);
        }
      }
    }
  }

  return QT_OK;
}

/* This is a little support function used to process the edit list when
 * building a frame table. */
#define MAX_DURATION 0x7FFFFFFFFFFFFFFF
static void get_next_edit_list_entry(qt_trak *trak, 
  int *edit_list_index,
  unsigned int *edit_list_media_time, 
  int64_t *edit_list_duration,
  unsigned int global_timescale) {

  /* if there is no edit list, set to max duration and get out */
  if (!trak->edit_list_table) {

    *edit_list_media_time = 0;
    *edit_list_duration = MAX_DURATION;
    debug_edit_list("  qt: no edit list table, initial = %d, %lld\n", *edit_list_media_time, *edit_list_duration);
    return;

  } else while (*edit_list_index < trak->edit_list_count) {

    /* otherwise, find an edit list entries whose media time != -1 */
    if (trak->edit_list_table[*edit_list_index].media_time != -1) {

      *edit_list_media_time = 
        trak->edit_list_table[*edit_list_index].media_time;
      *edit_list_duration = 
        trak->edit_list_table[*edit_list_index].track_duration;

      /* duration is in global timescale units; convert to trak timescale */
      *edit_list_duration *= trak->timescale;
      *edit_list_duration /= global_timescale;

      *edit_list_index = *edit_list_index + 1;
      break;
    }

    *edit_list_index = *edit_list_index + 1;
  }

  /* on the way out, check if this is the last edit list entry; if so, 
   * don't let the duration expire (so set it to an absurdly large value) 
   */
  if (*edit_list_index == trak->edit_list_count)
    *edit_list_duration = MAX_DURATION;
  debug_edit_list("  qt: edit list table exists, initial = %d, %lld\n", *edit_list_media_time, *edit_list_duration);
}

static qt_error build_frame_table(qt_trak *trak,
				  unsigned int global_timescale) {

  int i, j;
  unsigned int frame_counter;
  unsigned int chunk_start, chunk_end;
  unsigned int samples_per_chunk;
  uint64_t current_offset;
  int64_t current_pts;
  unsigned int pts_index;
  unsigned int pts_index_countdown;
  unsigned int audio_frame_counter = 0;
  unsigned int edit_list_media_time;
  int64_t edit_list_duration;
  int64_t frame_duration = 0;
  unsigned int edit_list_index;
  unsigned int edit_list_pts_counter;
  int atom_to_use;

  /* maintain counters for each of the subtracks within the trak */
  int *media_id_counts = NULL;

  if ((trak->type != MEDIA_VIDEO) &&
      (trak->type != MEDIA_AUDIO))
    return QT_OK;

  /* AUDIO and OTHER frame types follow the same rules; VIDEO and vbr audio
   * frame types follow a different set */
  if ((trak->type == MEDIA_VIDEO) || 
      (trak->properties->audio.vbr)) {

    /* in this case, the total number of frames is equal to the number of
     * entries in the sample size table */
    trak->frame_count = trak->sample_size_count;
    trak->frames = (qt_frame *)malloc(
      trak->frame_count * sizeof(qt_frame));
    if (!trak->frames)
      return QT_NO_MEMORY;
    trak->current_frame = 0;

    /* initialize more accounting variables */
    frame_counter = 0;
    current_pts = 0;
    pts_index = 0;
    pts_index_countdown =
      trak->time_to_sample_table[pts_index].count;

    media_id_counts = xine_xmalloc(trak->stsd_atoms_count * sizeof(int));
    if (!media_id_counts)
      return QT_NO_MEMORY;
    memset(media_id_counts, 0, trak->stsd_atoms_count * sizeof(int));

    /* iterate through each start chunk in the stsc table */
    for (i = 0; i < trak->sample_to_chunk_count; i++) {
      /* iterate from the first chunk of the current table entry to
       * the first chunk of the next table entry */
      chunk_start = trak->sample_to_chunk_table[i].first_chunk;
      if (i < trak->sample_to_chunk_count - 1)
        chunk_end =
          trak->sample_to_chunk_table[i + 1].first_chunk;
      else
        /* if the first chunk is in the last table entry, iterate to the
           final chunk number (the number of offsets in stco table) */
        chunk_end = trak->chunk_offset_count + 1;

      /* iterate through each sample in a chunk */
      for (j = chunk_start - 1; j < chunk_end - 1; j++) {

        samples_per_chunk =
          trak->sample_to_chunk_table[i].samples_per_chunk;
        current_offset = trak->chunk_offset_table[j];
        while (samples_per_chunk > 0) {

          /* media id accounting */
          if (trak->sample_to_chunk_table[i].media_id > trak->stsd_atoms_count) {
            printf ("QT: help! media ID out of range! (%d > %d)\n",
              trak->sample_to_chunk_table[i].media_id,
              trak->stsd_atoms_count);
            trak->frames[frame_counter].media_id = 0;
          } else {
            trak->frames[frame_counter].media_id = 
              trak->sample_to_chunk_table[i].media_id;
            media_id_counts[trak->sample_to_chunk_table[i].media_id - 1]++;
          }

          /* figure out the offset and size */
          trak->frames[frame_counter].offset = current_offset;
          if (trak->sample_size) {
            trak->frames[frame_counter].size =
              trak->sample_size;
            current_offset += trak->sample_size;
          } else {
            trak->frames[frame_counter].size =
              trak->sample_size_table[frame_counter];
            current_offset +=
              trak->sample_size_table[frame_counter];
          }

          /* if there is no stss (sample sync) table, make all of the frames
           * keyframes; otherwise, clear the keyframe bits for now */
          if (trak->sync_sample_table)
            trak->frames[frame_counter].keyframe = 0;
          else
            trak->frames[frame_counter].keyframe = 1;

          /* figure out the pts situation */
          trak->frames[frame_counter].pts = current_pts;
          current_pts +=
            trak->time_to_sample_table[pts_index].duration;
          pts_index_countdown--;
          /* time to refresh countdown? */
          if (!pts_index_countdown) {
            pts_index++;
            pts_index_countdown =
              trak->time_to_sample_table[pts_index].count;
          }

          samples_per_chunk--;
          frame_counter++;
        }
      }
    }

    /* fill in the keyframe information */
    if (trak->sync_sample_table) {
      for (i = 0; i < trak->sync_sample_count; i++)
        trak->frames[trak->sync_sample_table[i] - 1].keyframe = 1;
    }

    /* initialize edit list considerations */
    edit_list_index = 0;
    get_next_edit_list_entry(trak, &edit_list_index,
      &edit_list_media_time, &edit_list_duration, global_timescale);

    /* fix up pts information w.r.t. the edit list table */
    edit_list_pts_counter = 0;
    for (i = 0; i < trak->frame_count; i++) {

      debug_edit_list("    %d: (before) pts = %lld...", i, trak->frames[i].pts);

      if (trak->frames[i].pts < edit_list_media_time) 
        trak->frames[i].pts = edit_list_pts_counter;
      else {
        if (i < trak->frame_count - 1)
          frame_duration = 
            (trak->frames[i + 1].pts - trak->frames[i].pts);

        debug_edit_list("duration = %lld...", frame_duration);
        trak->frames[i].pts = edit_list_pts_counter;
        edit_list_pts_counter += frame_duration;
        edit_list_duration -= frame_duration;
      }

      debug_edit_list("(fixup) pts = %lld...", trak->frames[i].pts);

      /* reload media time and duration */
      if (edit_list_duration <= 0) {
        get_next_edit_list_entry(trak, &edit_list_index,
          &edit_list_media_time, &edit_list_duration, global_timescale);
      }

      debug_edit_list("(after) pts = %lld...\n", trak->frames[i].pts);
    }

    /* compute final pts values */
    for (i = 0; i < trak->frame_count; i++) {
      trak->frames[i].pts *= 90000;
      trak->frames[i].pts /= trak->timescale;
      debug_edit_list("  final pts for sample %d = %lld\n", i, trak->frames[i].pts);
    }

    /* decide which video properties atom to use */
    atom_to_use = 0;
    for (i = 1; i < trak->stsd_atoms_count; i++)
      if (media_id_counts[i] > media_id_counts[i - 1])
        atom_to_use = i;
    trak->properties = &trak->stsd_atoms[atom_to_use];

    /* adjust the stsd atom as needed */
    memcpy(trak->stsd + 12,
      &trak->stsd[trak->properties->video.properties_offset],
      BE_32(&trak->stsd[trak->properties->video.properties_offset]));

    free(media_id_counts);

  } else {

    /* in this case, the total number of frames is equal to the number of
     * chunks */
    trak->frame_count = trak->chunk_offset_count;
    trak->frames = (qt_frame *)malloc(
      trak->frame_count * sizeof(qt_frame));
    if (!trak->frames)
      return QT_NO_MEMORY;

    if (trak->type == MEDIA_AUDIO) {
      /* iterate through each start chunk in the stsc table */
      for (i = 0; i < trak->sample_to_chunk_count; i++) {
        /* iterate from the first chunk of the current table entry to
         * the first chunk of the next table entry */
        chunk_start = trak->sample_to_chunk_table[i].first_chunk;
        if (i < trak->sample_to_chunk_count - 1)
          chunk_end =
            trak->sample_to_chunk_table[i + 1].first_chunk;
        else
          /* if the first chunk is in the last table entry, iterate to the
             final chunk number (the number of offsets in stco table) */
          chunk_end = trak->chunk_offset_count + 1;

        /* iterate through each sample in a chunk and fill in size and
         * pts information */
        for (j = chunk_start - 1; j < chunk_end - 1; j++) {

          /* figure out the pts for this chunk */
          trak->frames[j].pts = audio_frame_counter;
          trak->frames[j].pts *= 90000;
          trak->frames[j].pts /= trak->timescale;

          /* fetch the alleged chunk size according to the QT header */
          trak->frames[j].size =
            trak->sample_to_chunk_table[i].samples_per_chunk;

          /* media id accounting */
          if (trak->sample_to_chunk_table[i].media_id > trak->stsd_atoms_count) {
            printf ("QT: help! media ID out of range! (%d > %d)\n",
              trak->sample_to_chunk_table[i].media_id,
              trak->stsd_atoms_count);
            trak->frames[j].media_id = 0;
          } else {
            trak->frames[j].media_id = 
              trak->sample_to_chunk_table[i].media_id;
          }

          /* the chunk size is actually the audio frame count */
          audio_frame_counter += trak->frames[j].size;

          /* compute the actual chunk size */
          trak->frames[j].size =
            (trak->frames[j].size *
             trak->properties->audio.channels) /
             trak->properties->audio.samples_per_frame *
             trak->properties->audio.bytes_per_frame;
        }
      }
    }

    /* fill in the rest of the information for the audio samples */
    for (i = 0; i < trak->frame_count; i++) {
      trak->frames[i].offset = trak->chunk_offset_table[i];
      trak->frames[i].keyframe = 0;
      if (trak->type != MEDIA_AUDIO)
        trak->frames[i].pts = 0;
    }
  }

  return QT_OK;
}

/*
 * This function takes a pointer to a qt_info structure and a pointer to
 * a buffer containing an uncompressed moov atom. When the function
 * finishes successfully, qt_info will have a list of qt_frame objects,
 * ordered by offset.
 */
static void parse_moov_atom(qt_info *info, unsigned char *moov_atom,
                            int64_t bandwidth) {
  int i, j;
  unsigned int moov_atom_size = BE_32(&moov_atom[0]);
  qt_atom current_atom;
  int string_size;
  unsigned int max_video_frames = 0;
  unsigned int max_audio_frames = 0;

  /* make sure this is actually a moov atom */
  if (BE_32(&moov_atom[4]) != MOOV_ATOM) {
    info->last_error = QT_NO_MOOV_ATOM;
    return;
  }

  /* prowl through the moov atom looking for very specific targets */
  for (i = ATOM_PREAMBLE_SIZE; i < moov_atom_size - 4; i++) {
    current_atom = BE_32(&moov_atom[i]);

    if (current_atom == MVHD_ATOM) {
      parse_mvhd_atom(info, &moov_atom[i - 4]);
      if (info->last_error != QT_OK)
        return;
      i += BE_32(&moov_atom[i - 4]) - 4;
    } else if (current_atom == TRAK_ATOM) {

      /* create a new trak structure */
      info->trak_count++;
      info->traks = (qt_trak *)realloc(info->traks, 
        info->trak_count * sizeof(qt_trak));

      parse_trak_atom (&info->traks[info->trak_count - 1], &moov_atom[i - 4]);
      if (info->last_error != QT_OK) {
        info->trak_count--;
        return;
      }
      i += BE_32(&moov_atom[i - 4]) - 4;

    } else if (current_atom == CPY_ATOM) {

      string_size = BE_16(&moov_atom[i + 4]) + 1;
      info->copyright = realloc (info->copyright, string_size);
      strncpy(info->copyright, &moov_atom[i + 8], string_size - 1);
      info->copyright[string_size - 1] = 0;

    } else if (current_atom == DES_ATOM) {

      string_size = BE_16(&moov_atom[i + 4]) + 1;
      info->description = realloc (info->description, string_size);
      strncpy(info->description, &moov_atom[i + 8], string_size - 1);
      info->description[string_size - 1] = 0;

    } else if (current_atom == CMT_ATOM) {

      string_size = BE_16(&moov_atom[i + 4]) + 1;
      info->comment = realloc (info->comment, string_size);
      strncpy(info->comment, &moov_atom[i + 8], string_size - 1);
      info->comment[string_size - 1] = 0;

    } else if (current_atom == RMDA_ATOM) {

      /* create a new reference structure */
      info->reference_count++;
      info->references = (reference_t *)realloc(info->references,
        info->reference_count * sizeof(reference_t));

      parse_reference_atom(&info->references[info->reference_count - 1],
        &moov_atom[i - 4], info->base_mrl);

    }
  }
  debug_atom_load("  qt: finished parsing moov atom\n");

  /* build frame tables corresponding to each trak */
  debug_frame_table("  qt: preparing to build %d frame tables\n",
    info->trak_count);
  for (i = 0; i < info->trak_count; i++) {

    debug_frame_table("    qt: building frame table #%d (%s)\n", i,
      (info->traks[i].type == MEDIA_VIDEO) ? "video" : "audio");
    build_frame_table(&info->traks[i], info->timescale);

    /* dump the frame table in debug mode */
    for (j = 0; j < info->traks[i].frame_count; j++)
      debug_frame_table("      %d: %8X bytes @ %llX, %lld pts, media id %d%s\n",
        j,
        info->traks[i].frames[j].size,
        info->traks[i].frames[j].offset,
        info->traks[i].frames[j].pts,
        info->traks[i].frames[j].media_id,
        (info->traks[i].frames[j].keyframe) ? " (keyframe)" : "");

    /* decide which audio trak and which video trak has the most frames */
    if ((info->traks[i].type == MEDIA_VIDEO) &&
        (info->traks[i].frame_count > max_video_frames)) {

      info->video_trak = i;
      max_video_frames = info->traks[i].frame_count;

    } else if ((info->traks[i].type == MEDIA_AUDIO) &&
               (info->traks[i].frame_count > max_audio_frames)) {

      info->audio_trak = i;
      max_audio_frames = info->traks[i].frame_count;
    }
  }

  /* check for references */
  if (info->reference_count > 0) {

    /* init chosen reference to the first entry */
    info->chosen_reference = 0;

    /* iterate through 1..n-1 reference entries and decide on the right one */
    for (i = 1; i < info->reference_count; i++) {

      if (info->references[i].qtim_version > 
          info->references[info->chosen_reference].qtim_version)
        info->chosen_reference = i;
      else if ((info->references[i].data_rate <= bandwidth) &&
               (info->references[i].data_rate > 
                info->references[info->chosen_reference].data_rate))
        info->chosen_reference = i;
    }

    debug_atom_load("  qt: chosen reference is ref #%d, qtim version %04X, %lld bps\n      URL: %s\n",
      info->chosen_reference,
      info->references[info->chosen_reference].qtim_version,
      info->references[info->chosen_reference].data_rate,
      info->references[info->chosen_reference].url);
  }
}

static qt_error open_qt_file(qt_info *info, input_plugin_t *input,
                             int64_t bandwidth) {

  unsigned char *moov_atom = NULL;
  off_t moov_atom_offset = -1;
  int64_t moov_atom_size = -1;
  unsigned char preview[MAX_PREVIEW_SIZE];

  /* zlib stuff */
  z_stream z_state;
  int z_ret_code;
  unsigned char *unzip_buffer;

  /* extract the base MRL if this is a http MRL */
  if (strncmp(input->get_mrl(input), "http://", 7) == 0) {

    char *slash;

    /* this will copy a few bytes too many, but no big deal */
    info->base_mrl = strdup(input->get_mrl(input));
    /* terminate the string after the last slash character */
    slash = strrchr(info->base_mrl, '/');
    if (slash)
      *(slash + 1) = '\0';
  }

  if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE))
    find_moov_atom(input, &moov_atom_offset, &moov_atom_size);
  else {
    input->get_optional_data(input, preview, INPUT_OPTIONAL_DATA_PREVIEW);
    if (BE_32(&preview[4]) != MOOV_ATOM) {
      info->last_error = QT_NO_MOOV_ATOM;
      return info->last_error;
    }
    moov_atom_offset = 0;
    moov_atom_size = BE_32(&preview[0]);
  }

  if (moov_atom_offset == -1) {
    info->last_error = QT_NO_MOOV_ATOM;
    return info->last_error;
  }
  info->moov_first_offset = moov_atom_offset;

  moov_atom = (unsigned char *)malloc(moov_atom_size);
  if (moov_atom == NULL) {
    info->last_error = QT_NO_MEMORY;
    return info->last_error;
  }

  /* seek to the start of moov atom */
  if (input->seek(input, info->moov_first_offset, SEEK_SET) !=
    info->moov_first_offset) {
    free(moov_atom);
    info->last_error = QT_FILE_READ_ERROR;
    return info->last_error;
  }
  if (input->read(input, moov_atom, moov_atom_size) != 
    moov_atom_size) {
    free(moov_atom);
    info->last_error = QT_FILE_READ_ERROR;
    return info->last_error;
  }

  /* check if moov is compressed */
  if (BE_32(&moov_atom[12]) == CMOV_ATOM) {

    info->compressed_header = 1;

    z_state.next_in = &moov_atom[0x28];
    z_state.avail_in = moov_atom_size - 0x28;
    z_state.avail_out = BE_32(&moov_atom[0x24]);
    unzip_buffer = (unsigned char *)malloc(BE_32(&moov_atom[0x24]));
    if (!unzip_buffer) {
      free(moov_atom);
      info->last_error = QT_NO_MEMORY;
      return info->last_error;
    }

    z_state.next_out = unzip_buffer;
    z_state.zalloc = (alloc_func)0;
    z_state.zfree = (free_func)0;
    z_state.opaque = (voidpf)0;

    z_ret_code = inflateInit (&z_state);
    if (Z_OK != z_ret_code) {
      free(unzip_buffer);
      free(moov_atom);
      info->last_error = QT_ZLIB_ERROR;
      return info->last_error;
    }

    z_ret_code = inflate(&z_state, Z_NO_FLUSH);
    if ((z_ret_code != Z_OK) && (z_ret_code != Z_STREAM_END)) {
      free(unzip_buffer);
      free(moov_atom);
      info->last_error = QT_ZLIB_ERROR;
      return info->last_error;
    }

    z_ret_code = inflateEnd(&z_state);
    if (Z_OK != z_ret_code) {
      free(unzip_buffer);
      free(moov_atom);
      info->last_error = QT_ZLIB_ERROR;
      return info->last_error;
    }

    /* replace the compressed moov atom with the decompressed atom */
    free (moov_atom);
    moov_atom = unzip_buffer;
    moov_atom_size = BE_32(&moov_atom[0]);
  }

  if (!moov_atom) {
    info->last_error = QT_NO_MOOV_ATOM;
    return info->last_error;
  }

  /* write moov atom to disk if debugging option is turned on */
  dump_moov_atom(moov_atom, moov_atom_size);

  /* take apart the moov atom */
  parse_moov_atom(info, moov_atom, bandwidth);
  if (info->last_error != QT_OK) {
    free(moov_atom);
    return info->last_error;
  }

  free(moov_atom);

  return QT_OK;
}

/**********************************************************************
 * xine demuxer functions
 **********************************************************************/

static int demux_qt_send_chunk(demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int i, j;
  unsigned int remaining_sample_bytes;
  int frame_duration;
  int first_buf;
  qt_trak *video_trak = NULL;
  qt_trak *audio_trak = NULL;
  int dispatch_audio;  /* boolean for deciding which trak to dispatch */
  int64_t pts_diff;
  xine_event_t uevent;
  xine_mrl_reference_data_t *data;

  /* check if it's time to send a reference up to the UI */
  if (this->qt->chosen_reference != -1) {

    uevent.type = XINE_EVENT_MRL_REFERENCE;
    uevent.stream = this->stream;
    uevent.data_length = 
      strlen(this->qt->references[this->qt->chosen_reference].url) +
      sizeof(xine_mrl_reference_data_t);
    data = malloc(uevent.data_length);
    uevent.data = data;
    strcpy(data->mrl, this->qt->references[this->qt->chosen_reference].url);
    data->alternative = 0;
    xine_event_send(this->stream, &uevent);
    free(data);

    this->status = DEMUX_FINISHED;
    return this->status;
  }

  if (this->qt->video_trak != -1) {
    video_trak = &this->qt->traks[this->qt->video_trak];
  }
  if (this->qt->audio_trak != -1) {
    audio_trak = &this->qt->traks[this->qt->audio_trak];
  }

  if (!audio_trak && !video_trak) {
    /* something is really wrong if this case is reached */
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* check if it is time to seek */
  if (this->qt->seek_flag) {
    this->qt->seek_flag = 0;

    /* if audio is present, send pts of current audio frame, otherwise
     * send current video frame pts */
    if (audio_trak)
      xine_demux_control_newpts(this->stream, 
        audio_trak->frames[audio_trak->current_frame].pts, 
        BUF_FLAG_SEEK);
    else
      xine_demux_control_newpts(this->stream, 
        video_trak->frames[video_trak->current_frame].pts, 
        BUF_FLAG_SEEK);
  }

  /* Decide the trak from which to dispatch a frame. Policy: Dispatch
   * the frames in offset order as much as possible. If the pts difference
   * between the current frames from the audio and video traks is too
   * wide, make an exception. This exception deals with non-interleaved
   * Quicktime files. */
  if (!audio_trak) {

    /* only video is present */
    dispatch_audio = 0;
    if (video_trak->current_frame >= video_trak->frame_count) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

  } else if (!video_trak) {

    /* only audio is present */
    dispatch_audio = 1;
    if (audio_trak->current_frame >= audio_trak->frame_count) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

  } else {

    /* both audio and video are present; start making some tough choices */

    /* check the frame count limits */
    if ((audio_trak->current_frame >= audio_trak->frame_count) &&
        (video_trak->current_frame >= video_trak->frame_count)) {

      this->status = DEMUX_FINISHED;
      return this->status;

    } else if (video_trak->current_frame >= video_trak->frame_count) {

      dispatch_audio = 1;

    } else if (audio_trak->current_frame >= audio_trak->frame_count) {

      dispatch_audio = 0;

    } else {

      /* at this point, it is certain that both traks still have frames
       * yet to be dispatched */
      pts_diff  = audio_trak->frames[audio_trak->current_frame].pts;
      pts_diff -= video_trak->frames[video_trak->current_frame].pts;

      if (pts_diff > MAX_PTS_DIFF) {
        /* if diff is +max_diff, audio is too far ahead of video */
        dispatch_audio = 0;
      } else if (pts_diff < -MAX_PTS_DIFF) {
        /* if diff is -max_diff, video is too far ahead of audio */
        dispatch_audio = 1;
      } else if (audio_trak->frames[audio_trak->current_frame].offset <
                 video_trak->frames[video_trak->current_frame].offset) {
        /* pts diff is not too wide, decide based on earlier offset */
        dispatch_audio = 1;
      } else {
        dispatch_audio = 0;
      }
    }
  }

  if (!dispatch_audio) {
    i = video_trak->current_frame++;

    if (video_trak->frames[i].media_id != video_trak->properties->video.media_id) {
      this->status = DEMUX_OK;
      return this->status;
    }

    remaining_sample_bytes = video_trak->frames[i].size;
    this->input->seek(this->input, video_trak->frames[i].offset,
      SEEK_SET);

    if (i + 1 < video_trak->frame_count) {
      /* frame duration is the pts diff between this video frame and
       * the next video frame */
      frame_duration  = video_trak->frames[i + 1].pts;
      frame_duration -= video_trak->frames[i].pts;
    } else {
      /* give the last frame some fixed duration */
      frame_duration = 12000;
    }

    /* Due to the edit lists, some successive frames have the same pts
     * which would ordinarily cause frame_duration to be 0 which can
     * cause DIV-by-0 errors in the engine. Perform this little trick
     * to compensate. */
    if (!frame_duration) {
      frame_duration = 1;
      video_trak->properties->video.edit_list_compensation++;
    } else {
      frame_duration -= video_trak->properties->video.edit_list_compensation;
      video_trak->properties->video.edit_list_compensation = 0;
    }

    this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION] =
      frame_duration;

    debug_video_demux("  qt: sending off video frame %d from offset 0x%llX, %d bytes, media id %d, %lld pts\n",
      i, 
      video_trak->frames[i].offset,
      video_trak->frames[i].size,
      video_trak->frames[i].media_id,
      video_trak->frames[i].pts);

    while (remaining_sample_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = video_trak->properties->video.codec_buftype;
      buf->extra_info->input_pos = video_trak->frames[i].offset - this->data_start;
      buf->extra_info->input_length = this->data_size;
      buf->extra_info->input_time = video_trak->frames[i].pts / 90;
      buf->pts = video_trak->frames[i].pts;

      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = frame_duration;

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

      if (video_trak->frames[i].keyframe)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;
      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      this->video_fifo->put(this->video_fifo, buf);
    }

  } else {
    /* load an audio sample and packetize it */
    i = audio_trak->current_frame++;

    if (audio_trak->frames[i].media_id != audio_trak->properties->audio.media_id) {
      this->status = DEMUX_OK;
      return this->status;
    }

    /* only go through with this procedure if audio_fifo exists */
    if (!this->audio_fifo)
      return this->status;

    remaining_sample_bytes = audio_trak->frames[i].size;
    this->input->seek(this->input, audio_trak->frames[i].offset,
      SEEK_SET);

    debug_audio_demux("  qt: sending off audio frame %d from offset 0x%llX, %d bytes, media id %d, %lld pts\n",
      i, 
      audio_trak->frames[i].offset,
      audio_trak->frames[i].size,
      audio_trak->frames[i].media_id,
      audio_trak->frames[i].pts);

    first_buf = 1;
    while (remaining_sample_bytes) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = audio_trak->properties->audio.codec_buftype;
      buf->extra_info->input_pos = audio_trak->frames[i].offset - this->data_start;
      buf->extra_info->input_length = this->data_size;
      /* The audio chunk is often broken up into multiple 8K buffers when
       * it is sent to the audio decoder. Only attach the proper timestamp
       * to the first buffer. This is for the linear PCM decoder which
       * turns around and sends out audio buffers as soon as they are
       * received. If 2 or more consecutive audio buffers are dispatched to
       * the audio out unit, the engine will compensate with pops. */
      if ((buf->type == BUF_AUDIO_LPCM_BE) || 
          (buf->type == BUF_AUDIO_LPCM_LE)) { 
        if (first_buf) {
          buf->extra_info->input_time = audio_trak->frames[i].pts / 90;
          buf->pts = audio_trak->frames[i].pts;
          first_buf = 0;
        } else {
          buf->extra_info->input_time = 0;
          buf->pts = 0;
        }
      } else {
        buf->extra_info->input_time = audio_trak->frames[i].pts / 90;
        buf->pts = audio_trak->frames[i].pts;
      }

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

      /* Special case alert: If this is signed, 8-bit data, transform
       * the data to unsigned. */
      if ((audio_trak->properties->audio.bits == 8) && 
          ((audio_trak->properties->audio.codec_fourcc == TWOS_FOURCC) ||
           (audio_trak->properties->audio.codec_fourcc == SOWT_FOURCC)))
        for (j = 0; j < buf->size; j++)
          buf->content[j] += 0x80;

      if (!remaining_sample_bytes) {
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
      }

      this->audio_fifo->put(this->audio_fifo, buf);
    }
  }

  return this->status;
}

static void demux_qt_send_headers(demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf;
  qt_trak *video_trak = NULL;
  qt_trak *audio_trak = NULL;

  /* for deciding data start and data size */
  int64_t first_video_offset = -1;
  int64_t  last_video_offset = -1;
  int64_t first_audio_offset = -1;
  int64_t  last_audio_offset = -1;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* figure out where the data begins and ends */
  if (this->qt->video_trak != -1) {
    video_trak = &this->qt->traks[this->qt->video_trak];
    first_video_offset = video_trak->frames[0].offset;
    last_video_offset = video_trak->frames[video_trak->frame_count - 1].size +
      video_trak->frames[video_trak->frame_count - 1].offset;
  }
  if (this->qt->audio_trak != -1) {
    audio_trak = &this->qt->traks[this->qt->audio_trak];
    first_audio_offset = audio_trak->frames[0].offset;
    last_audio_offset = audio_trak->frames[audio_trak->frame_count - 1].size +
      audio_trak->frames[audio_trak->frame_count - 1].offset;
  }

  if (first_video_offset < first_audio_offset)
    this->data_start = first_video_offset;
  else
    this->data_start = first_audio_offset;

  if (last_video_offset > last_audio_offset)
    this->data_size = last_video_offset - this->data_size;
  else
    this->data_size = last_audio_offset - this->data_size;

  /* sort out the A/V information */
  if (this->qt->video_trak != -1) {

    this->bih.biSize = sizeof(this->bih);
    this->bih.biWidth = video_trak->properties->video.width;
    this->bih.biHeight = video_trak->properties->video.height;
    this->bih.biBitCount = video_trak->properties->video.depth;

    this->bih.biCompression = video_trak->properties->video.codec_fourcc;
    video_trak->properties->video.codec_buftype = 
      fourcc_to_buf_video(this->bih.biCompression);

    /* hack: workaround a fourcc clash! 'mpg4' is used by MS and Sorenson
     * mpeg4 codecs (they are not compatible).
     */
    if( video_trak->properties->video.codec_buftype == BUF_VIDEO_MSMPEG4_V1 )
      video_trak->properties->video.codec_buftype = BUF_VIDEO_MPEG4;
  
    if( !video_trak->properties->video.codec_buftype && 
         video_trak->properties->video.codec_fourcc )
      video_trak->properties->video.codec_buftype = BUF_VIDEO_UNKNOWN;

    this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] =
      this->bih.biWidth;
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = 
      this->bih.biHeight;
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_FOURCC] = 
      video_trak->properties->video.codec_fourcc;

  } else {

    memset(&this->bih, 0, sizeof(this->bih));
    this->bih.biSize = sizeof(this->bih);
    this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = 0;
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = 0;
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_FOURCC] = 0;

  }

  if (this->qt->audio_trak != -1) {

    audio_trak->properties->audio.codec_buftype = 
      formattag_to_buf_audio(audio_trak->properties->audio.codec_fourcc);

    if( !audio_trak->properties->audio.codec_buftype &&
         audio_trak->properties->audio.codec_fourcc )
      audio_trak->properties->audio.codec_buftype = BUF_AUDIO_UNKNOWN;

    this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
      audio_trak->properties->audio.channels;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
      audio_trak->properties->audio.sample_rate;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
      audio_trak->properties->audio.bits;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_FOURCC] = 
      audio_trak->properties->audio.codec_fourcc;

  } else {

    this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 0;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = 0;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] = 0;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] = 0;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_FOURCC] = 0;

  }

  /* copy over the meta information like artist and title */
  if (this->qt->copyright)
    this->stream->meta_info[XINE_META_INFO_ARTIST] =
      strdup(this->qt->copyright);
  if (this->qt->description)
    this->stream->meta_info[XINE_META_INFO_TITLE] =
      strdup(this->qt->description);
  if (this->qt->comment)
    this->stream->meta_info[XINE_META_INFO_COMMENT] =
      strdup(this->qt->comment);

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  if (video_trak &&
      (video_trak->properties->video.codec_buftype)) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    /* initial video step; not necessary since each QT frame has its own
     * duration, but set it non-zero as a matter of custom */
    buf->decoder_info[1] = 3000;
    memcpy(buf->content, &this->bih, sizeof(this->bih));
    buf->size = sizeof(this->bih);
    buf->type = video_trak->properties->video.codec_buftype;
    this->video_fifo->put (this->video_fifo, buf);
      
    /* send header info to decoder. some mpeg4 streams need this */
    if( video_trak->decoder_config ) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = video_trak->properties->video.codec_buftype;
      buf->size = video_trak->decoder_config_len;
      buf->content = video_trak->decoder_config;      
      this->video_fifo->put (this->video_fifo, buf);
    }

    /* send off the palette, if there is one */
    if (video_trak->properties->video.palette_count) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->decoder_flags = BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
      buf->decoder_info[2] = video_trak->properties->video.palette_count;
      buf->decoder_info_ptr[2] = &video_trak->properties->video.palette;
      buf->size = 0;
      buf->type = video_trak->properties->video.codec_buftype;
      this->video_fifo->put (this->video_fifo, buf);
    }

    /* send stsd to the decoder */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_SPECIAL;
    buf->decoder_info[1] = BUF_SPECIAL_STSD_ATOM;
    buf->decoder_info[2] = video_trak->stsd_size;
    buf->decoder_info_ptr[2] = video_trak->stsd;
    buf->size = 0;
    buf->type = video_trak->properties->video.codec_buftype;
    this->video_fifo->put (this->video_fifo, buf);
  }

  if ((this->qt->audio_trak != -1) &&
      (audio_trak->properties->audio.codec_buftype) &&
      this->audio_fifo) {

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = audio_trak->properties->audio.codec_buftype;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = audio_trak->properties->audio.sample_rate;
    buf->decoder_info[2] = audio_trak->properties->audio.bits;
    buf->decoder_info[3] = audio_trak->properties->audio.channels;
    buf->content = (void *)&audio_trak->properties->audio.wave;
    buf->size = sizeof(audio_trak->properties->audio.wave);
    this->audio_fifo->put (this->audio_fifo, buf);
    
    if( audio_trak->decoder_config ) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = audio_trak->properties->audio.codec_buftype;
      buf->size = 0;
      buf->decoder_flags = BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_DECODER_CONFIG;
      buf->decoder_info[2] = audio_trak->decoder_config_len;
      buf->decoder_info_ptr[2] = audio_trak->decoder_config;
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    /* send stsd to the decoder */
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->decoder_flags = BUF_FLAG_SPECIAL;
    buf->decoder_info[1] = BUF_SPECIAL_STSD_ATOM;
    buf->decoder_info[2] = audio_trak->stsd_size;
    buf->decoder_info_ptr[2] = audio_trak->stsd;
    buf->size = 0;
    buf->type = audio_trak->properties->audio.codec_buftype;
    this->audio_fifo->put (this->audio_fifo, buf);

  }
}

/* support function that performs a binary seek on a trak; returns the
 * demux status */
static int binary_seek(qt_trak *trak, off_t start_pos, int start_time) {

  int best_index;
  int left, middle, right;
  int found;

  /* perform a binary search on the trak, testing the offset
   * boundaries first; offset request has precedent over time request */
  if (start_pos) {
    if (start_pos <= trak->frames[0].offset)
      best_index = 0;
    else if (start_pos >= trak->frames[trak->frame_count - 1].offset)
      return DEMUX_FINISHED;
    else {
      left = 0;
      right = trak->frame_count - 1;
      found = 0;

      while (!found) {
	middle = (left + right + 1) / 2;
        if ((start_pos >= trak->frames[middle].offset) &&
            (start_pos < trak->frames[middle + 1].offset)) {
          found = 1;
        } else if (start_pos < trak->frames[middle].offset) {
          right = middle - 1;
        } else {
          left = middle;
        }
      }

      best_index = middle;
    }
  } else {
    int64_t pts = 90000 * start_time;

    if (pts <= trak->frames[0].pts)
      best_index = 0;
    else if (pts >= trak->frames[trak->frame_count - 1].pts)
      return DEMUX_FINISHED;
    else {
      left = 0;
      right = trak->frame_count - 1;
      do {
	middle = (left + right + 1) / 2;
	if (pts < trak->frames[middle].pts) {
	  right = (middle - 1);
	} else {
	  left = middle;
	}
      } while (left < right);

      best_index = left;
    }
  }

  trak->current_frame = best_index;
  return DEMUX_OK;
}

static int demux_qt_seek (demux_plugin_t *this_gen,
                          off_t start_pos, int start_time) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  qt_trak *video_trak = NULL;
  qt_trak *audio_trak = NULL;

  int64_t keyframe_pts;

  /* short-circuit any attempts to seek in a non-seekable stream, including
   * seeking in the forward direction; this may change later */
  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) == 0) {
    this->qt->seek_flag = 1;
    this->status = DEMUX_OK;
    return this->status;
  }

  /* if there is a video trak, position it as close as possible to the
   * requested position */
  if (this->qt->video_trak != -1) {
    video_trak = &this->qt->traks[this->qt->video_trak];
    this->status = binary_seek(video_trak, start_pos, start_time);
    if (this->status != DEMUX_OK)
      return this->status;
  }

  if (this->qt->audio_trak != -1) {
    audio_trak = &this->qt->traks[this->qt->audio_trak];
    this->status = binary_seek(audio_trak, start_pos, start_time);
    if (this->status != DEMUX_OK)
      return this->status;
  }

  /* search back in the video trak for the nearest keyframe */
  if (video_trak)
    while (video_trak->current_frame) {
      if (video_trak->frames[video_trak->current_frame].keyframe) {
        break;
      }
      video_trak->current_frame--;
    }

  /* not done yet; now that the nearest keyframe has been found, seek
   * back to the first audio frame that has a pts less than or equal to
   * that of the keyframe; do not go through with this process there is
   * no video trak */
  if (audio_trak && video_trak) {
    keyframe_pts = video_trak->frames[video_trak->current_frame].pts;
    while (audio_trak->current_frame) {
      if (audio_trak->frames[audio_trak->current_frame].pts < keyframe_pts) {
        break;
      }
      audio_trak->current_frame--;
    }
  }

  this->qt->seek_flag = 1;
  this->status = DEMUX_OK;

  /*
   * do only flush if already running (seeking).
   * otherwise decoder_config is flushed too.
   */
  if(this->stream->demux_thread_running)
    xine_demux_flush_engine(this->stream);

  return this->status;
}

static void demux_qt_dispose (demux_plugin_t *this_gen) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  free_qt_info(this->qt);
  free(this);
}

static int demux_qt_get_status (demux_plugin_t *this_gen) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  return this->status;
}

static int demux_qt_get_stream_length (demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;

  return (int)((int64_t) 1000 * this->qt->duration / this->qt->timescale);
}

static uint32_t demux_qt_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_qt_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_qt_t     *this;
  xine_cfg_entry_t entry;

  if ((input->get_capabilities(input) & INPUT_CAP_BLOCK)) {
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_qt_t));
  this->stream = stream;
  this->input  = input;

  /* fetch bandwidth config */
  this->bandwidth = 0x7FFFFFFFFFFFFFFF;  /* assume infinite bandwidth */
  if (xine_config_lookup_entry (stream->xine, "input.mms_network_bandwidth",
                                &entry)) {
    if ((entry.num_value >= 0) && (entry.num_value <= 11))
      this->bandwidth = bandwidths[entry.num_value];
  }

  this->demux_plugin.send_headers      = demux_qt_send_headers;
  this->demux_plugin.send_chunk        = demux_qt_send_chunk;
  this->demux_plugin.seek              = demux_qt_seek;
  this->demux_plugin.dispose           = demux_qt_dispose;
  this->demux_plugin.get_status        = demux_qt_get_status;
  this->demux_plugin.get_stream_length = demux_qt_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_qt_get_capabilities;
  this->demux_plugin.get_optional_data = demux_qt_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:

    if (!is_qt_file(this->input)) {
      free (this);
      return NULL;
    }
    if ((this->qt = create_qt_info()) == NULL) {
      free (this);
      return NULL;
    }
    if (open_qt_file(this->qt, this->input, this->bandwidth) != QT_OK) {
      free_qt_info (this->qt);
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

    if (strncasecmp (ending, ".mov", 4) &&
        strncasecmp (ending, ".qt", 3) &&
        strncasecmp (ending, ".mp4", 4)) {
      free (this);
      return NULL;
    }
  }

  /* we want to fall through here */
  case METHOD_EXPLICIT: {

    if (!is_qt_file(this->input)) {
      free (this);
      return NULL;
    }
    if ((this->qt = create_qt_info()) == NULL) {
      free (this);
      return NULL;
    }
    if (open_qt_file(this->qt, this->input, this->bandwidth) != QT_OK) {
      free_qt_info (this->qt);
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
  return "Apple Quicktime (MOV) and MPEG-4 demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "MOV/MPEG-4";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "mov qt mp4";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "video/quicktime: mov,qt: Quicktime animation;"
         "video/x-quicktime: mov,qt: Quicktime animation;"
         "application/x-quicktimeplayer: qtl: Quicktime list;";
}

static void class_dispose (demux_class_t *this_gen) {

  demux_qt_class_t *this = (demux_qt_class_t *) this_gen;

  free (this);
}

void *demux_qt_init_plugin (xine_t *xine, void *data) {

  demux_qt_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_qt_class_t));
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

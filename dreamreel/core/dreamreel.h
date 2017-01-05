#ifndef _XINE_INTERNAL_H
#define _XINE_INTERNAL_H

/* stdio.h at least buys us off_t */
#include <stdio.h>
/* malloc, free */
#include <stdlib.h>
/* strdup et al. */
#include <string.h>
#include <inttypes.h>

#include <kos.h>

#include "video_out.h"

#define DEBUG_PRINTF 0
#if DEBUG_PRINTF
#define debug_printf printf
#else
static inline void debug_printf(const char *format, ...) { }
#endif

/* forward type definitions */
typedef struct xine_stream_s xine_stream_t;
typedef struct xine_s xine_t;

typedef struct {
  char      *origin; /* file plugin: path */
  char      *mrl;    /* <type>://<location> */
  char      *link;
  uint32_t   type;   /* see below */
  off_t      size;   /* size of this source, may be 0 */
} xine_mrl_t;

/*
 * extra_info_t is used to pass information from input or demuxer plugins
 * to output frames (past decoder). new data must be added after the existing
 * fields for backward compatibility.
 */
typedef struct extra_info_s {

  off_t    input_pos; /* remember where this buf came from in the input source */
  off_t    input_length; /* remember the length of the input source */
  int      input_time; /* time offset in miliseconds from beginning of stream */
  uint32_t frame_number; /* number of current frame if known */

  int      seek_count; /* internal engine use */
  int64_t  vpts;       /* set on output layers only */

  int      invalid;    /* do not use this extra info to update anything */

} extra_info_t;

typedef struct config_values_s {

} config_values_t;

typedef struct {
  int                 alternative; /* alternative playlist number, usually 0 */
  char                mrl[1]; /* might (will) be longer */
} xine_mrl_reference_data_t;

#define XINE_EVENT_MRL_REFERENCE          9 /* demuxer->frontend: MRL reference(s) for the real stream */

typedef struct {
  int                              type;   /* event type (constants see above) */
  xine_stream_t                   *stream; /* stream this event belongs to     */

  void                            *data;   /* contents depending on type */
  int                              data_length;

  /* you do not have to provide this, it will be filled in by xine_event_send()*/
  struct timeval                   tv;     /* timestamp of event creation */
} xine_event_t;

typedef struct xine_cfg_entry_s xine_cfg_entry_t;

typedef void (*xine_config_cb_t) (void *user_data,
                                  xine_cfg_entry_t *entry);
struct xine_cfg_entry_s {
  const char      *key;     /* unique id (example: gui.logo_mrl) */

  int              type;

  /* type unknown */
  char            *unknown_value;

  /* type string */
  char            *str_value;
  char            *str_default;
  char            *str_sticky;

  /* common to range, enum, num, bool: */
  int              num_value;
  int              num_default;

  /* type range specific: */
  int              range_min;
  int              range_max;

  /* type enum specific: */
  char           **enum_values;

  /* help info for the user */
  const char      *description;
  const char      *help;

  /* user experience level */
  int              exp_level; /* 0 => beginner,
                                10 => advanced user,
                                20 => expert */

  /* callback function and data for live changeable values */
  xine_config_cb_t callback;
  void            *callback_data;

};

/**************************************************************************
 * buffer API
 **************************************************************************/

#include "buffer.h"

/**************************************************************************
 * input API
 **************************************************************************/

typedef struct input_class_s input_class_t;
typedef struct input_plugin_s input_plugin_t;

struct input_plugin_s {

  /*
   * return capabilities of the current playable entity. See
   * get_current_pos below for a description of a "playable entity"
   * Capabilities a created by "OR"ing a mask of constants listed
   * below which start "INPUT_CAP".
   *
   * depending on the values set, some of the functions below
   * will or will not get called or should (not) be able to
   * do certain tasks.
   *
   * for example if INPUT_CAP_SEEKABLE is set,
   * the seek() function is expected to work fully at any time.
   * however, if the flag is not set, the seek() function should
   * make a best-effort attempt to seek, e.g. at least
   * relative forward seeking should work.
   */
  uint32_t (*get_capabilities) (input_plugin_t *this);

  /*
   * read nlen bytes, return number of bytes read
   */
  off_t (*read) (input_plugin_t *this, char *buf, off_t nlen);

  /*
   * read one block, return newly allocated block (or NULL on failure)
   * for blocked input sources len must be == blocksize
   * the fifo parameter is only used to get access to the buffer_pool_alloc function
   */
  buf_element_t *(*read_block)(input_plugin_t *this, fifo_buffer_t *fifo, off_t len);

  /*
   * seek position, return new position
   *
   * if seeking failed, -1 is returned
   */
  off_t (*seek) (input_plugin_t *this, off_t offset, int origin);


  /*
   * get current position in stream.
   *
   */
  off_t (*get_current_pos) (input_plugin_t *this);

  /*
   * return number of bytes in the next playable entity or -1 if the
   * input is unlimited, as would be the case in a network stream.
   *
   * A "playable entity" tends to be the entities listed in a playback
   * list or the units on which playback control generally works on.
   * It might be the number of bytes in a VCD "segment" or "track" (if
   * the track has no "entry" subdivisions), or the number of bytes in
   * a PS (Program Segment or "Chapter") of a DVD. If there are no
   * subdivisions of the input medium and it is considered one
   * indivisible entity, it would be the byte count of that entity;
   * for example, the length in bytes of an MPEG file.

   * This length information is used, for example when in setting the
   * absolute or relative play position or possibly calculating the
   * bit rate.
   */
  off_t (*get_length) (input_plugin_t *this);

  /*
   * return block size in bytes of next complete playable entity (if
   * supported, 0 otherwise). See the description above under
   * get_length for a description of a "complete playable entity".
   *
   * this block size is only used for mpeg streams stored on
   * a block oriented storage media, e.g. DVDs and VCDs, to speed
   * up the demuxing process. only set this (and the INPUT_CAP_BLOCK
   * flag) if this is the case for your input plugin.
   *
   * make this function simply return 0 if unsure.
   */
  uint32_t (*get_blocksize) (input_plugin_t *this);


  /*
   * return current MRL
   */
  char * (*get_mrl) (input_plugin_t *this);


  /*
   * request optional data from input plugin.
   */
  int (*get_optional_data) (input_plugin_t *this, void *data, int data_type);


  /*
   * close stream, free instance resources
   */
  void (*dispose) (input_plugin_t *this);

  /*
   * "backward" link to input plugin class struct
   */

  input_class_t *input_class;

};

struct input_class_s {

  /*
   * open a new instance of this plugin class
   */
  input_plugin_t* (*open_plugin) (input_class_t *this, xine_stream_t *stream, const char *mrl);

  /*
   * return short, human readable identifier for this plugin class
   */
  char* (*get_identifier) (input_class_t *this);

  /*
   * return human readable (verbose = 1 line) description for
   * this plugin class
   */
  char* (*get_description) (input_class_t *this);

  /*
   * ls function, optional: may be NULL
   * return value: NULL => filename is a file, **char=> filename is a dir
   */
  xine_mrl_t ** (*get_dir) (input_class_t *this, const char *filename, int *nFiles);

  /*
   * generate autoplay list, optional: may be NULL
   * return value: list of MRLs
   */
  char ** (*get_autoplay_list) (input_class_t *this, int *num_files);

  /*
   * close down, free all resources
   */
  void (*dispose) (input_class_t *this);

  /*
   * eject/load the media (if possible), optional: may be NULL
   *
   * returns 0 for temporary failures
   */
  int (*eject_media) (input_class_t *this);

};

/*
 * possible capabilites an input plugin can have:
 */
#define INPUT_CAP_NOCAP                0x00000000

/*
 * INPUT_CAP_SEEKABLE:
 *   seek () works reliably.
 *   even for plugins that do not have this flag set
 *   it is a good idea to implement the seek() function
 *   in a "best effort" style anyway, so at least
 *   throw away data for network streams when seeking forward
 */

#define INPUT_CAP_SEEKABLE             0x00000001

/*
 * INPUT_CAP_BLOCK:
 *   means more or less that a block device sits behind
 *   this input plugin. get_blocksize must be implemented.
 *   will be used for fast and efficient demuxing of
 *   mpeg streams (demux_mpeg_block).
 */

#define INPUT_CAP_BLOCK                0x00000002

/*
 * INPUT_CAP_AUDIOLANG:
 * INPUT_CAP_SPULANG:
 *   input plugin knows something about audio/spu languages,
 *   e.g. knows that audio stream #0 is english,
 *   audio stream #1 is german, ...
 *   *((int *)data) will provide the requested channel number
 *   and awaits the language back in (char *)data
 */

#define INPUT_CAP_AUDIOLANG            0x00000008
#define INPUT_CAP_SPULANG              0x00000010

/*
 * INPUT_CAP_PREVIEW:
 *   get_optional_data can handle INPUT_OPTIONAL_DATA_PREVIEW
 *   so a non-seekable stream plugin can povide the first
 *   few bytes for demuxers to look at them and decide wheter
 *   they can handle the stream or not. the preview data must
 *   be buffered and delivered again through subsequent
 *   read() calls.
 *   caller must provide a buffer allocated with at least
 *   MAX_PREVIEW_SIZE bytes.
 */

#define INPUT_CAP_PREVIEW              0x00000040

/*
 * INPUT_CAP_CHAPTERS:
 *   The media streams provided by this plugin have an internal
 *   structure dividing it into segments usable for navigation.
 *   For those plugins, the behaviour of the skip button in UIs
 *   should be changed from "next MRL" to "next chapter" by
 *   sending XINE_EVENT_INPUT_NEXT.
 */

#define INPUT_CAP_CHAPTERS             0x00000080


#define INPUT_OPTIONAL_UNSUPPORTED    0
#define INPUT_OPTIONAL_SUCCESS        1

#define INPUT_OPTIONAL_DATA_AUDIOLANG 2
#define INPUT_OPTIONAL_DATA_SPULANG   3
#define INPUT_OPTIONAL_DATA_PREVIEW   7

#define MAX_MRL_ENTRIES 255
#define MAX_PREVIEW_SIZE 4096

/**************************************************************************
 * demux API
 **************************************************************************/

#include "demux.h"



/**************************************************************************
 * core xine API
 **************************************************************************/

void *xine_xmalloc(size_t size);

#define XINE_STREAM_INFO_MAX 99

/*
 * the "big" xine struct, holding everything together
 */
struct xine_s {

  config_values_t           *config;

  int                        demux_strategy;

  int                        verbosity;

//  xine_list_t               *streams;
//  pthread_mutex_t            streams_lock;

//  metronom_clock_t          *clock;

};

struct xine_stream_s {

  xine_t                    *xine;

  int                        status;

  input_plugin_t            *input;
  input_class_t             *eject_class;
  int                        content_detection_method;
  demux_plugin_t            *demux;

  fifo_buffer_t              *video_fifo;
  fifo_buffer_t              *audio_fifo;

  /* stream meta information */
  int                        stream_info[XINE_STREAM_INFO_MAX];
  char                      *meta_info  [XINE_STREAM_INFO_MAX];

  kthread_t                 *video_decoder_thread;
  kthread_t                 *audio_decoder_thread;
  kthread_t                 *video_output_thread;
  kthread_t                 *audio_output_thread;
  kthread_t                 *demux_thread;
  int                        demux_thread_running;

};

/* xine_get_stream_info */
#define XINE_STREAM_INFO_BITRATE           0
#define XINE_STREAM_INFO_SEEKABLE          1
#define XINE_STREAM_INFO_VIDEO_WIDTH       2
#define XINE_STREAM_INFO_VIDEO_HEIGHT      3
#define XINE_STREAM_INFO_VIDEO_RATIO       4 /* *10000 */
#define XINE_STREAM_INFO_VIDEO_CHANNELS    5
#define XINE_STREAM_INFO_VIDEO_STREAMS     6
#define XINE_STREAM_INFO_VIDEO_BITRATE     7
#define XINE_STREAM_INFO_VIDEO_FOURCC      8
#define XINE_STREAM_INFO_VIDEO_HANDLED     9  /* codec available? */
#define XINE_STREAM_INFO_FRAME_DURATION    10 /* 1/90000 sec */
#define XINE_STREAM_INFO_AUDIO_CHANNELS    11
#define XINE_STREAM_INFO_AUDIO_BITS        12
#define XINE_STREAM_INFO_AUDIO_SAMPLERATE  13
#define XINE_STREAM_INFO_AUDIO_BITRATE     14
#define XINE_STREAM_INFO_AUDIO_FOURCC      15
#define XINE_STREAM_INFO_AUDIO_HANDLED     16 /* codec available? */
#define XINE_STREAM_INFO_HAS_CHAPTERS      17
#define XINE_STREAM_INFO_HAS_VIDEO         18
#define XINE_STREAM_INFO_HAS_AUDIO         19
#define XINE_STREAM_INFO_IGNORE_VIDEO      20
#define XINE_STREAM_INFO_IGNORE_AUDIO      21
#define XINE_STREAM_INFO_IGNORE_SPU        22
#define XINE_STREAM_INFO_VIDEO_HAS_STILL   23
#define XINE_STREAM_INFO_MAX_AUDIO_CHANNEL 24
#define XINE_STREAM_INFO_MAX_SPU_CHANNEL   25
#define XINE_STREAM_INFO_AUDIO_MODE        26

/* xine_get_meta_info */
#define XINE_META_INFO_TITLE               0
#define XINE_META_INFO_COMMENT             1
#define XINE_META_INFO_ARTIST              2
#define XINE_META_INFO_GENRE               3
#define XINE_META_INFO_ALBUM               4
#define XINE_META_INFO_YEAR                5
#define XINE_META_INFO_VIDEOCODEC          6
#define XINE_META_INFO_AUDIOCODEC          7
#define XINE_META_INFO_SYSTEMLAYER         8
#define XINE_META_INFO_INPUT_PLUGIN        9

/* "plugin" catalog data structures */
typedef struct {
  uint8_t     type;               /* one of the PLUGIN_* constants above     */
  uint8_t     API;                /* API version supported by this plugin    */
  char       *id;                 /* a name that identifies this plugin      */
  uint32_t    version;            /* version number, increased every release */
  void       *special_info;       /* plugin-type specific, see structs below */
  void       *(*init)(xine_t *, void *); /* init the plugin class            */
  void       *plugin_class;       /* the actual plugin class                 */
} plugin_info_t;

#define PLUGIN_NONE           0
#define PLUGIN_INPUT          1
#define PLUGIN_DEMUX          2

/**************************************************************************
 * other
 **************************************************************************/

void xine_log (xine_t *self, int buf,
               const char *format, ...);

/* don't worry about translation */
#define _(x) x

#define XINE_LOG_MSG       0 /* warnings, errors, ... */
#define XINE_LOG_PLUGIN    1
#define XINE_LOG_NUM       2 /* # of log buffers defined */

/* verbosity settings */
#define XINE_VERBOSITY_NONE                0
#define XINE_VERBOSITY_LOG                 1
#define XINE_VERBOSITY_DEBUG               2

void xine_demux_flush_engine         (xine_stream_t *stream);
void xine_demux_control_newpts       (xine_stream_t *stream, int64_t pts, uint32_t flags);
void xine_demux_control_headers_done (xine_stream_t *stream);
void xine_demux_control_start        (xine_stream_t *stream);
void xine_demux_control_end          (xine_stream_t *stream, uint32_t flags);

#endif

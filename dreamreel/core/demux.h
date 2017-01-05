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
 * $Id: demux.h,v 1.29 2003/01/10 21:10:50 miguelfreitas Exp $
 */

#ifndef HAVE_DEMUX_H
#define HAVE_DEMUX_H

#include "buffer.h"
#include "xine_internal.h"
#include "input_plugin.h"

#define DEMUXER_PLUGIN_IFACE_VERSION    20

#define DEMUX_OK                   0
#define DEMUX_FINISHED             1

#define DEMUX_CANNOT_HANDLE        0
#define DEMUX_CAN_HANDLE           1

#define METHOD_BY_CONTENT          1
#define METHOD_BY_EXTENSION        2
#define METHOD_EXPLICIT            3

typedef struct demux_class_s demux_class_t ;
typedef struct demux_plugin_s demux_plugin_t;

struct demux_class_s {

  /*
   * open a new instance of this plugin class
   */
  demux_plugin_t* (*open_plugin) (demux_class_t *this, xine_stream_t *stream, input_plugin_t *input);

  /*
   * return human readable (verbose = 1 line) description for this plugin
   */
  char* (*get_description) (demux_class_t *this);

  /*
   * return human readable identifier for this plugin
   */

  char* (*get_identifier) (demux_class_t *this);
  
  /*
   * return MIME types supported for this plugin
   */

  char* (*get_mimetypes) (demux_class_t *this);

  /*
   * return ' ' seperated list of file extensions this
   * demuxer is likely to handle
   * (will be used to filter media files in 
   * file selection dialogs)
   */

  char* (*get_extensions) (demux_class_t *this);

  /*
   * close down, free all resources
   */
  void (*dispose) (demux_class_t *this);
};


/*
 * any demux plugin must implement these functions
 */

struct demux_plugin_s {

  /*
   * send headers, followed by BUF_CONTROL_HEADERS_DONE down the
   * fifos, then return. do not start demux thread (yet)
   */

  void (*send_headers) (demux_plugin_t *this);

  /*
   * ask demux to seek 
   *
   * for seekable streams, a start position can be specified
   *
   * start_pos  : position in input source
   * start_time : position measured in seconds from stream start
   *
   * if both parameters are !=0 start_pos will be used
   * for non-seekable streams both values will be ignored
   *
   * returns the demux status (like get_status, but immediately after
   *                           starting the demuxer)
   */

  int (*seek) (demux_plugin_t *this, 
	       off_t start_pos, int start_time);

  /*
   * send a chunk of data down to decoder fifos 
   *
   * the meaning of "chunk" is specific to every demux, usually
   * it involves parsing one unit of data from stream.
   *
   * this function will be called from demux loop and should return
   * the demux current status
   */

  int (*send_chunk) (demux_plugin_t *this);
          
  /*
   * free resources 
   */

  void (*dispose) (demux_plugin_t *this) ;

  /*
   * returns DEMUX_OK or  DEMUX_FINISHED 
   */

  int (*get_status) (demux_plugin_t *this) ;

  /*
   * gets stream length in miliseconds (might be estimated)
   * may return 0 for non-seekable streams
   */

  int (*get_stream_length) (demux_plugin_t *this);

  /*
   * get audio/video frames 
   *
   * experimental, function pointers can be NULL for now.
   */

  int (*get_video_frame) (demux_plugin_t *this,
			  int timestamp, /* msec */
			  int *width, int *height,
			  int *ratio_code, 
			  int *duration, /* msec */
			  int *format,
			  uint8_t *img) ;

  /* called by video_out for every frame it receives */
  void (*got_video_frame_cb) (demux_plugin_t *this,
			      vo_frame_t *frame);

  /*
   * return capabilities of demuxed stream
   */

  uint32_t (*get_capabilities) (demux_plugin_t *this);
  
  /*
   * request optional data from input plugin.
   */
  int (*get_optional_data) (demux_plugin_t *this, void *data, int data_type);
  
  /*
   * "backwards" link to plugin class
   */

  demux_class_t *demux_class;
} ;

/*
 * possible capabilites a demux plugin can have:
 */
#define DEMUX_CAP_NOCAP                0x00000000

/*
 * DEMUX_CAP_AUDIOLANG:
 * DEMUX_CAP_SPULANG:
 *   demux plugin knows something about audio/spu languages, 
 *   e.g. knows that audio stream #0 is english, 
 *   audio stream #1 is german, ...  Same bits as INPUT
 *   capabilities .
 */

#define DEMUX_CAP_AUDIOLANG            0x00000008
#define DEMUX_CAP_SPULANG              0x00000010


#define DEMUX_OPTIONAL_UNSUPPORTED    0
#define DEMUX_OPTIONAL_SUCCESS        1

#define DEMUX_OPTIONAL_DATA_AUDIOLANG 2
#define DEMUX_OPTIONAL_DATA_SPULANG   3

#endif

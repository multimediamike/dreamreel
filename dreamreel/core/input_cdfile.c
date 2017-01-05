/*
 * CD Filesystem (ISO-9660) Input Plugin for Dreamedia
 */

#include "dreamreel.h"

typedef struct {
  input_plugin_t       input_plugin;

  xine_stream_t       *stream;

  file_t fd;
  char *mrl;

} cdfile_input_plugin_t;

typedef struct {

  input_class_t        input_class;

  xine_t              *xine;
  config_values_t     *config;

} cdfile_input_class_t;

static uint32_t cdfile_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_SEEKABLE;
}

static off_t cdfile_plugin_read (input_plugin_t *this_gen, char *buf, 
  off_t len) {

  cdfile_input_plugin_t *this = (cdfile_input_plugin_t *) this_gen;

  return (off_t)fs_read(this->fd, buf, len);
}

static buf_element_t *cdfile_plugin_read_block (input_plugin_t *this_gen, 
  fifo_buffer_t *fifo, off_t nlen) {

  return NULL;
}

static off_t cdfile_plugin_seek (input_plugin_t *this_gen, off_t offset, 
  int origin) {

  cdfile_input_plugin_t *this = (cdfile_input_plugin_t *) this_gen;

  return fs_seek(this->fd, offset, origin);
}

static off_t cdfile_plugin_get_current_pos (input_plugin_t *this_gen){

  cdfile_input_plugin_t *this = (cdfile_input_plugin_t *) this_gen;

  return fs_tell(this->fd);
}

static off_t cdfile_plugin_get_length (input_plugin_t *this_gen) {

  cdfile_input_plugin_t *this = (cdfile_input_plugin_t *) this_gen;

  return (off_t)fs_total(this->fd);
}

static uint32_t cdfile_plugin_get_blocksize (input_plugin_t *this_gen) {

  return 0;
}

static char* cdfile_plugin_get_mrl (input_plugin_t *this_gen) {
  cdfile_input_plugin_t *this = (cdfile_input_plugin_t *) this_gen;

  return this->mrl;
}

static int cdfile_plugin_get_optional_data (input_plugin_t *this_gen,
                                          void *data, int data_type) {
  return 0;
}

static void cdfile_plugin_dispose (input_plugin_t *this_gen ) {

  cdfile_input_plugin_t *this = (cdfile_input_plugin_t *) this_gen;

  fs_close(this->fd);

  free(this->mrl);

  free(this);
}

static input_plugin_t *open_plugin (input_class_t *cls_gen, xine_stream_t *stream,
                                    const char *data) {

  cdfile_input_plugin_t *this;
  file_t fd;

  /* qualify the MRL */
  if (strncasecmp (data, "file://", 7) != 0)
    return NULL;

  /* skip to the filename and open the file */
  fd = fs_open(&data[6], O_RDONLY);
  if (!fd)
    return NULL;

  this = (cdfile_input_plugin_t *) xine_xmalloc (sizeof (cdfile_input_plugin_t));
  this->stream = stream;

  this->fd = fd;
  
  this->input_plugin.get_capabilities   = cdfile_plugin_get_capabilities;
  this->input_plugin.read               = cdfile_plugin_read;
  this->input_plugin.read_block         = cdfile_plugin_read_block;
  this->input_plugin.seek               = cdfile_plugin_seek;
  this->input_plugin.get_current_pos    = cdfile_plugin_get_current_pos;
  this->input_plugin.get_length         = cdfile_plugin_get_length;
  this->input_plugin.get_blocksize      = cdfile_plugin_get_blocksize;
  this->input_plugin.get_mrl            = cdfile_plugin_get_mrl;
  this->input_plugin.get_optional_data  = cdfile_plugin_get_optional_data;
  this->input_plugin.dispose            = cdfile_plugin_dispose;
  this->input_plugin.input_class        = cls_gen;

  this->mrl = strdup(data);

  return &this->input_plugin;
}

static char ** cdfile_class_get_autoplay_list (input_class_t *this_gen, 
					    int *num_files) {

  return NULL;
}

static char *cdfile_class_get_identifier (input_class_t *this_gen) {

  return "cdfile";

}

static char *cdfile_class_get_description (input_class_t *this_gen) {

  return "KOS/Sega Dreamcast CD-ROM Filesystem Input";

}

static xine_mrl_t **cdfile_class_get_dir (input_class_t *this_gen,
                                          const char *filename, int *nFiles) {

  return NULL;
}

static void cdfile_class_dispose (input_class_t *this_gen) {

  cdfile_input_class_t  *this = (cdfile_input_class_t *) this_gen;

  free (this);
}

void *cdfile_init_plugin (xine_t *xine, void *data) {

  cdfile_input_class_t  *this;
  config_values_t     *config;

  this = (cdfile_input_class_t *) xine_xmalloc (sizeof (cdfile_input_class_t));

  this->xine   = xine;
  this->config = xine->config;
  config       = xine->config;

  this->input_class.open_plugin        = open_plugin;
  this->input_class.get_identifier     = cdfile_class_get_identifier;
  this->input_class.get_description    = cdfile_class_get_description;
  this->input_class.get_dir            = cdfile_class_get_dir;
  this->input_class.get_autoplay_list  = cdfile_class_get_autoplay_list;
  this->input_class.dispose            = cdfile_class_dispose;
  this->input_class.eject_media        = NULL;

  return this;
}

/*
 * Dreamreel User Interface Module
 * This is module but not a thread. It processes and maintains the state of
 * the user interface.
 *
 * Public functions:
 *  init_elements()
 *  update_elements(io_status_t)
 *  draw_elements()
 */

#include <kos.h>
#include "gui.h"

/**************************************************************************
 * file globals
 **************************************************************************/

/* This mutex guards against both the update() and draw() functions
 * occurring at the same time. */
static mutex_t *ui_mutex;

/**************************************************************************
 * public functions
 **************************************************************************/

void init_elements() {

  ui_mutex = mutex_create();

  /* load all UI elements into VRAM */

}

void update_elements(io_status_t *status) {

  mutex_lock(ui_mutex);

  mutex_unlock(ui_mutex);
}

void draw_elements(void) {
  mutex_lock(ui_mutex);

  mutex_unlock(ui_mutex);
}

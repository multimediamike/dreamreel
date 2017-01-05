/*
 * metronom.c
 *
 * This module implements the timer callback facility for Dreamreel.
 */

#include <kos.h>

#include "dreamreel.h"
#include "metronom.h"

/* total vblank counter */
static int counter = 0;

static volatile int metronom_started;
/*static*/ volatile int64_t pts_counter;
static int pts_inc;
static int vblank_handler_handle;

/*static*/ volatile int64_t next_video_pts;
static void (*video_pts_callback)(void);

static volatile int64_t next_audio_pts;
static void (*audio_pts_callback)(void);

void set_next_video_pts(int64_t next_pts, void (*callback)(void)) {

  next_video_pts = next_pts;
debug_printf ("  set next video pts for %lld, vbl counter = %d\n", next_pts, counter);
  video_pts_callback = callback;
}

void set_next_audio_pts(int64_t next_pts, void (*callback)(void)) {

  next_audio_pts = next_pts;
  audio_pts_callback = callback;
}

static void vblank_handler(uint32 code) {
counter++;

  if (metronom_started) {

    if (pts_counter >= next_video_pts) {
//      next_video_pts = MAX_PTS;
      video_pts_callback();
    }

    if (pts_counter >= next_audio_pts) {
      next_audio_pts = MAX_PTS;
      audio_pts_callback();
    }

    pts_counter += pts_inc;

  }
}

void metronom_set(int64_t new_pts) {

debug_printf ("    metronon_set: %lld\n", new_pts);
  pts_counter = new_pts;
}

/* This function kicks off the periodic interrupt based on the refresh rate
 * and sets up the pts ISR. */
void init_metronom(void) {

  next_video_pts = next_audio_pts = MAX_PTS;
  pts_counter = 0;
  metronom_started = 0;

  /* install a vblank handler */
  vblank_handler_handle = vblank_handler_add(vblank_handler);

//  if (ntsc)
    pts_inc = 90000 / 60;
//  else
//    pts_inc = 90000 / 50;
}

void start_metronom(void) {

debug_printf ("  metronom started (vbl counter = %d)\n", counter);
  metronom_started = 1;
}

void stop_metronom(void) {

  metronom_started = 0;
}

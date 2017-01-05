#include <kos.h>

#include "dreamreel.h"
#include "video_out.h"
#include "metronom.h"
#include "gui.h"

/**************************************************************************
 * global variables borrowed from video_decoder.c
 **************************************************************************/

/* these are the actual dimensions of the image */
extern int actual_width, actual_height;

/* these are the texture dimensions of the image */
extern int texture_width, texture_height;
extern int log2_width, log2_height;
extern int texture_size;

/**************************************************************************
 * video output global variables
 **************************************************************************/

/* coordinates of the top-left and bottom-right corners of the frame */
static int ul_x, ul_y;
static int br_x, br_y;

extern enum PixelFormat pixel_format;

/* These variables manage the video output frames represented as textures
 * in VRAM. */
#define MAX_VRAM_TEXTURES 20
static vo_frame_t vram_textures[MAX_VRAM_TEXTURES];
static int vram_texture_count;
static int next_free_vram_texture;
static int current_vram_texture;
static int next_output_vram_texture;

/* These 2 textures are used as intermediate buffers for twiddling before
 * the textures are sent on to VRAM. The unaligned textures are the raw
 * allocation while the twiddle_textures are aligned on a 32-byte
 * boundary for DMA. */
static unsigned char *twiddle_textures_unaligned[2];
static unsigned char *twiddle_textures[2];
static mutex_t *twiddle_texture_mutex[2];
static int active_twiddle_texture;

static volatile int thread_is_alive = 0;
static volatile int deliver_next_frame;
static kthread_t *this_thread;
static int frame_queued;

static int64_t video_pts;

/*
 * 0 = stopped
 * 1 = play
 * 2 = paused
 */
static int thread_state;

/**************************************************************************
 * video output functions
 **************************************************************************/

static void video_output_callback(void) {

  deliver_next_frame = 1;

  /* push this thread to the front of the line */
  thd_schedule_next(this_thread);
}

/* returns 0 if everything checked out */
int init_video_out(void) {

  int i;
  float width_ratio, height_ratio;

  /* do not proceed if the thread has not started yet */
  while (!thread_is_alive)
    thd_pass();

  /* reset the video output for good measure */
  reset_video_out();

  /* allocate VRAM and main RAM work buffers */
  twiddle_textures_unaligned[0] = malloc(texture_size + 32);
  twiddle_textures_unaligned[1] = malloc(texture_size + 32);
  if (!twiddle_textures_unaligned[0] || !twiddle_textures_unaligned[1])
    goto free_memory;

  /* find the aligned addresses for the sake of DMA */
  for (i = 0; i < 32; i++) {
    if ((((unsigned int)twiddle_textures_unaligned[0] + i) & 0x1F) == 0)
      twiddle_textures[0] = twiddle_textures_unaligned[0] + i;
    if ((((unsigned int)twiddle_textures_unaligned[1] + i) & 0x1F) == 0)
      twiddle_textures[1] = twiddle_textures_unaligned[1] + i;
  }

  /* allocate as many textures as available VRAM will allow */
  for (i = 0; i < MAX_VRAM_TEXTURES; i++) {
    if ((vram_textures[i].base[0] = pvr_mem_malloc(texture_size)) == NULL)
      break;
    vram_textures[i].pts = -1;
  }
  vram_texture_count = i;
  next_free_vram_texture = 0;

  /* determine the boundaries of the texture */
  width_ratio = 640.0 / actual_width;
  height_ratio = 480.0 / actual_height;

  /* compute the stretched resolution */
  if (width_ratio < height_ratio) {
    ul_x = 0;
    br_x = (int)(width_ratio * texture_width);

    ul_y = ((480 - width_ratio * actual_height) / 2);
    br_y = ul_y + (int)(width_ratio * texture_height);
  } else {
  }

printf ("  init_video_out: stretch resolution = (%d, %d) -> (%d, %d)\n", ul_x, ul_y, br_x, br_y);

  return 0;

free_memory:
  free(twiddle_textures_unaligned[0]);
  free(twiddle_textures_unaligned[1]);

  return 1;
}

int reset_video_out(void) {

  int i;

  free(twiddle_textures_unaligned[0]);
  free(twiddle_textures_unaligned[1]);
  twiddle_textures_unaligned[0] = twiddle_textures_unaligned[1] = NULL;
  twiddle_textures[0] = twiddle_textures[1] = NULL;

  for (i = 0; i < vram_texture_count; i++) {
    pvr_mem_free(vram_textures[i].base[0]);
    pvr_mem_free(vram_textures[i].base[1]);
    pvr_mem_free(vram_textures[i].base[2]);
    pvr_mem_free(vram_textures[i].base[3]);
  }

  memset(vram_textures, 0, MAX_VRAM_TEXTURES * sizeof(vo_frame_t));

  vram_texture_count = 0;
  next_free_vram_texture = 0;
  current_vram_texture = 0;
  next_output_vram_texture = 0;

  return 0;
}

/* This function locks 1 of the 2 twiddle textures. If both of the work
 * textures are locked (one texture is being DMA'd and the other is ready
 * for DMA) this function blocks until one of the textures is ready. */
void lock_twiddle_texture(void) {

debug_printf ("    video_out: locking work texture...\n");
  /* if no VRAM frames are ready, wait until the next one is */
  while (vram_textures[next_free_vram_texture].in_use)
    thd_pass();

  /* wait for one of the work textures to become available; this is the
   * only function that can lock one of these mutexes so when one of them
   * becomes available it will not be locked before this function locks it */
  while (mutex_is_locked(twiddle_texture_mutex[0]) &&
         mutex_is_locked(twiddle_texture_mutex[1]))
    thd_pass();

  if (mutex_is_locked(twiddle_texture_mutex[0])) {
    mutex_lock(twiddle_texture_mutex[1]);
    active_twiddle_texture = 1;
  } else {
    mutex_lock(twiddle_texture_mutex[0]);
    active_twiddle_texture = 0;
  }

  /* lock the current frame */
  current_vram_texture = next_free_vram_texture;
  next_free_vram_texture = (next_free_vram_texture + 1) % vram_texture_count;
debug_printf ("    video_out: locked work texture %d and VRAM texture %d\n",
  active_twiddle_texture, current_vram_texture);
}

/* This function mimics the libavcodec draw_horiz_band() function:
 *  src_ptr contains pointers to 1 or 3 planes of image data
 *  linesize is the width of a single line in memory; if this is planar
 *    YUV data, linesize refers to the width of the Y data
 *  y is the starting Y axis position of the slice
 *  width is the actual width of the image data
 *  h is the height of the slice
 */

/* borrowing liberally from 
 * kos/kernel/arch/dreamcast/hardware/pvr/pvr_texture.c
 * for the texture twiddling */
#define TWIDTAB(x) ( (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)| \
        ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )
#define MIN(a, b) ( (a)<(b)? (a):(b) )

void draw_texture_slice(
  uint8_t **src_ptr, int linesize,
  int start_y, int width, int height) {

  uint8 * pixels;
  uint16 * vtex;
  int x, y;
  int min, mask, yout;

debug_printf ("    video_out: drawing work texture...\n");

  /* twiddle the texture into a work frame in main RAM */
  min = MIN(width, height);
  mask = min - 1;
  pixels = src_ptr[0];
  vtex = (uint16*)twiddle_textures[active_twiddle_texture];
  for (y = 0; y < height; y += 2) {
    yout = y;
    for (x = 0; x < width; x++) {
#if 1
      vtex[TWIDOUT((yout & mask) / 2, x & mask) +
        ((x / min + yout / min) * min * min / 2)] =
        pixels[y * width + x] | (pixels[(y + 1) * width + x] << 8);
#else
      vtex[TWIDOUT((yout & mask) / 2, x & mask) +
        ((x + yout) * min / 2)] =
        pixels[y * width + x] | (pixels[(y + 1) * width + x] << 8);
#endif
    }
  }
}

/* This function tells the video output module that the active texture is
 * finished and ready to be moved out to VRAM. If there is a palette change,
 * set palette_change to 1 and pass an array of 256 PVR color ints via
 * *palette. */
void send_texture(int64_t pts, int64_t vpts, int new_palette, 
  int *palette, int last_frame) {

debug_printf ("    video_out: sending work texture...\n");
  vram_textures[current_vram_texture].pts = pts;
  vram_textures[current_vram_texture].vpts = vpts;
  vram_textures[current_vram_texture].last_frame = last_frame;
  vram_textures[current_vram_texture].new_palette = new_palette;
  if (new_palette)
    memcpy(vram_textures[current_vram_texture].palette, palette, 256 * 4);

  /* send the twiddled texture out to VRAM */
/* it's blocking right now */
#if 1
  pvr_txr_load_dma(twiddle_textures[active_twiddle_texture],
    vram_textures[current_vram_texture].base[0], texture_size, 1);
#else
  pvr_txr_load(
    twiddle_textures[active_twiddle_texture], 
    vram_textures[current_vram_texture].base[0],
    texture_size);
#endif

  /* free the work frame */
  mutex_unlock(twiddle_texture_mutex[active_twiddle_texture]);

  vram_textures[current_vram_texture].in_use = 1;
debug_printf ("    video_out: work texture sent...\n");

  /* if thread is stopped, transition to play state but wait for a period
   * of time to buffer before delivering frame */
  if (thread_state == 0) {
    thread_state = 1;
    frame_queued = 1;
    set_next_video_pts(vram_textures[next_output_vram_texture].vpts, 
      video_output_callback);
debug_printf ("    video_out: starting metronom @ pts %lld for frame @ pts %lld\n",
  vram_textures[current_vram_texture].vpts - 9000,
  vram_textures[current_vram_texture].vpts);
    metronom_set(vram_textures[current_vram_texture].vpts - 9000);
    start_metronom();
  }
}

void display_logo(void) {

  pvr_poly_cxt_t cxt;
  pvr_poly_hdr_t hdr;
  pvr_vertex_t vert;
  unsigned short texture[8 * 8];
//  unsigned char *texture;
  int i, x, y;
  pvr_ptr_t logo_texture;
  uint32 fd;

#if 0
printf (" trying to display logo...\n");
  fd = fs_open("/rd/dreamree.bmp", O_RDONLY);
  if (!fd) {
    printf ("could not open logo file\n");
    return;
  }
  texture = (unsigned char *)fs_mmap(fd);
  if (!texture) {
    printf ("could not mmap logo file\n");
    return;
  }
#endif

  for (y = 0; y < 4; y++) {
    for (x = 0; x < 4; x++) {
      texture[y * 8 + x] = 0xFFFF;  // white
      texture[y * 8 + x + 4] = 0xFC00;  // red
      texture[(y + 4) * 8 + x] = 0x83E0;  // green
      texture[(y + 4) * 8 + x + 4] = 0x801F;  // blue
    }
  }
  logo_texture = pvr_mem_malloc(8 * 8 * 2);
  pvr_txr_load_ex(texture, logo_texture, 8, 8, PVR_TXRLOAD_16BPP);

#if 0
  /* load the palette */
  texture += 0x36;
  for (i = 0; i < 16; i++) {
    pvr_set_pal_entry(i, *(unsigned int*)texture);
    texture += 4;
  }

  /* by now, the texture should be pointing to the logo data */
  logo_texture = pvr_mem_malloc(160 * 40 / 2);
  pvr_txr_load_ex(texture, logo_texture, 160, 40, PVR_TXRLOAD_4BPP);
#endif

  /* prep the PVR hardware */
  pvr_wait_ready();
  pvr_scene_begin();
  pvr_set_bg_color(0, 0, 0);

  pvr_list_begin(PVR_LIST_OP_POLY);

  pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_ARGB1555, 8, 8,
    logo_texture, PVR_FILTER_NONE);
  pvr_poly_compile(&hdr, &cxt);
  pvr_prim(&hdr, sizeof(hdr));

  vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
  vert.oargb = 0;
  vert.flags = PVR_CMD_VERTEX;

  vert.x = 100;
  vert.y = 100;
  vert.z = 1;
  vert.u = 0.0;
  vert.v = 0.0;
  pvr_prim(&vert, sizeof(vert));

  vert.x = 300;
  vert.y = 100;
  vert.z = 1;
  vert.u = 1.0;
  vert.v = 0.0;
  pvr_prim(&vert, sizeof(vert));

  vert.x = 100;
  vert.y = 200;
  vert.z = 1;
  vert.u = 0.0;
  vert.v = 1.0;
  pvr_prim(&vert, sizeof(vert));

  vert.x = 300;
  vert.y = 200;
  vert.z = 1;
  vert.u = 1.0;
  vert.v = 1.0;
  vert.flags = PVR_CMD_VERTEX_EOL;
  pvr_prim(&vert, sizeof(vert));

  pvr_list_finish();

  pvr_scene_finish();

  /* do this sequence to flip it out */
  pvr_wait_ready();
  pvr_set_bg_color(0, 0, 0);
  pvr_scene_begin();
  pvr_scene_finish();
}

/**************************************************************************
 * video output thread
 **************************************************************************/

void video_output_thread(void *v) {

  int i;
  pvr_poly_cxt_t cxt;
  pvr_poly_hdr_t hdr;
  pvr_vertex_t vert;

debug_printf ("  *** this is the video output thread talking\n");

  this_thread = thd_get_current();

  /* set up the video output hardware */
  pvr_init_defaults();
  pvr_dma_init();
  pvr_set_pal_format(PVR_PAL_ARGB8888);

  /* let the UI initialize itself */
  init_elements();

  /* initialize all of the video output variables */
  memset(vram_textures, 0, MAX_VRAM_TEXTURES * sizeof(vo_frame_t));
  twiddle_textures_unaligned[0] = NULL;
  twiddle_textures_unaligned[1] = NULL;
  twiddle_textures[0] = NULL;
  twiddle_textures[1] = NULL;
  twiddle_texture_mutex[0] = mutex_create();
  twiddle_texture_mutex[1] = mutex_create();

  next_output_vram_texture = 0;
  deliver_next_frame = 0;
  frame_queued = 0;
  thread_state = 0;

  display_logo();

  /* by now, the thread is initialized and running */
  thread_is_alive = 1;

  while (thread_is_alive) {

    if (deliver_next_frame) {

debug_printf ("    video_out: delivering frame\n");
      /* if the palette needs to be reprogrammed, do it */
      if (vram_textures[next_output_vram_texture].new_palette) {
        for (i = 0; i < 256; i++) {
          pvr_set_pal_entry(i, vram_textures[next_output_vram_texture].palette[i]);
        }
      }

      /* program the PVR to display the next frame */
      /* prep the PVR hardware */
      pvr_wait_ready();
      pvr_scene_begin();

      pvr_list_begin(PVR_LIST_OP_POLY);

      pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_PAL8BPP, 
        texture_width, texture_height,
        vram_textures[next_output_vram_texture].base[0], PVR_FILTER_BILINEAR);
      cxt.txr.format |= PVR_TXRFMT_8BPP_PAL(0);
      pvr_poly_compile(&hdr, &cxt);
      pvr_prim(&hdr, sizeof(hdr));

      vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
      vert.oargb = 0;
      vert.flags = PVR_CMD_VERTEX;

      vert.x = ul_x;
      vert.y = ul_y;
      vert.z = 1;
      vert.u = 0.0;
      vert.v = 0.0;
      pvr_prim(&vert, sizeof(vert));

      vert.x = br_x;
      vert.y = ul_y;
      vert.z = 1;
      vert.u = 1.0;
      vert.v = 0.0;
      pvr_prim(&vert, sizeof(vert));

      vert.x = ul_x;
      vert.y = br_y;
      vert.z = 1;
      vert.u = 0.0;
      vert.v = 1.0;
      pvr_prim(&vert, sizeof(vert));

      vert.x = br_x;
      vert.y = br_y;
      vert.z = 1;
      vert.u = 1.0;
      vert.v = 1.0;
      vert.flags = PVR_CMD_VERTEX_EOL;
      pvr_prim(&vert, sizeof(vert));

      pvr_list_finish();

      /* let the GUI draw whatever it needs to */
//      gui_draw();

      /* let the PVR do its thing */
      pvr_scene_finish();

      /* free the frame that was just displayed */
/* NOTE: may not want to do this until PVR is finished */
      vram_textures[next_output_vram_texture].in_use = 0;
debug_printf ("    video out: delivered frame %d\n", next_output_vram_texture);

      /* if the last frame was just delivered, stop the metronom */
      if (vram_textures[next_output_vram_texture].last_frame)
        stop_metronom();

      /* let the metronom know when the next frame needs to be delivered,
       * if there is a frame to deliver */
      next_output_vram_texture = (next_output_vram_texture + 1) % vram_texture_count;
      if (vram_textures[next_output_vram_texture].in_use) {
        frame_queued = 1;
        set_next_video_pts(vram_textures[next_output_vram_texture].vpts, 
          video_output_callback);
debug_printf ("    video_out: drew frame, now queueing frame %d for pts %lld\n",
  next_output_vram_texture, vram_textures[next_output_vram_texture].vpts);
      } else {
        frame_queued = 0;
      }

      deliver_next_frame = 0;

    } else if ((thread_state == 1) &&
               (vram_textures[next_output_vram_texture].in_use) &&
               (!frame_queued)) {

      /* in playback state, no frame is queued, but frame is ready */

      /* set the metronom callback if a new frame has become ready */
      frame_queued = 1;
      set_next_video_pts(vram_textures[next_output_vram_texture].vpts, 
        video_output_callback);
debug_printf ("    video_out: queueing next frame for pts %lld\n",
  vram_textures[next_output_vram_texture].vpts);
    }

    /* handle the DMA done notification */
//    if (dma is done) {
//    }


    /* do not spend anymore time in this thread than necessary */
    thd_pass();
  }


debug_printf ("video output thread exit\n");

}

#if 0
void stop_video_out_thread(void) {

  thread_is_alive = 0;
}

int all_video_frames_ready(void) {

  return vram_textures[next_free_vram_texture].in_use;
}

void start_video_playback(void) {

/*
  frame_queued = 1;
  thread_state = 1;
*/
debug_printf (" start_video_playback() called, doing nothing...\n");
}

void stop_video_playback(void) {

/*
  playback_state = 0;
*/
debug_printf (" stop_video_playback() called, doing nothing...\n");
}
#endif

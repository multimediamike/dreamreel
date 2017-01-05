/*
 *
 * Copyright (C) 2003 the ffmpeg project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Autodesk Animator "FLIC" (and derivatives) Video Decoder
 *   by Mike Melanson (melanson@pcisys.net)
 * See this site for more information of FLIC and its derivatives:
 *   http://www.compuphase.com/flic.htm
 *
 */

/**
 * @file flic.c
 * Autodesk Animator FLIC Video Decoder.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "bswap.h"

#define FLI_256_COLOR 4
#define FLI_DELTA     7
#define FLI_COLOR     11
#define FLI_LC        12
#define FLI_BLACK     13
#define FLI_BRUN      15
#define FLI_COPY      16
#define FLI_MINI      18

typedef struct FlicDecodeContext {
    AVCodecContext *avctx;
    int width, height;
    AVFrame frame;
} FlicDecodeContext;

static int flic_decode_init(AVCodecContext *avctx)
{
    FlicDecodeContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->width = avctx->width;
    s->height = avctx->height;
    avctx->pix_fmt = PIX_FMT_PAL8;
    avctx->has_b_frames = 0;

    /* allocate the single buffer that will be needed */
    if (avctx->get_buffer(avctx, &s->frame) < 0) {
        fprintf(stderr, "get_buffer() failed\n");
        return -1;
    }

    return 0;
}

static int flic_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    FlicDecodeContext *s=avctx->priv_data;

    int stream_ptr = 0;
    int stream_ptr_after_color_chunk;
    int pixel_ptr;
    int palette_ptr1;
    int palette_ptr2;
    unsigned char palette_idx1;
    unsigned char palette_idx2;

    unsigned int frame_size;
    int num_chunks;

    unsigned int chunk_size;
    int chunk_type;

    int i, j;

    int color_packets;
    int color_changes;
    int color_shift;
    unsigned char r, g, b;

    int lines, x;
    int compressed_lines;
    int starting_line;
    signed short line_packets;
    int y_ptr;
    signed char byte_run;
    int pixel_skip;
    int update_whole_frame = 0;   /* palette change flag */
    int ghost_pixel_ptr;
    int ghost_y_ptr;
    int pixel_countdown;
//int color_shifter = buf[0] | (buf[1] << 8) | (buf[0x10] << 16);

    frame_size = LE_32(&buf[stream_ptr]);
    stream_ptr += 6;  /* skip the magic number */
    num_chunks = LE_16(&buf[stream_ptr]);
    stream_ptr += 10;  /* skip padding */

    /* iterate through the chunks */
    frame_size -= 16;
    while ((frame_size > 0) && (num_chunks > 0)) {
        chunk_size = LE_32(&buf[stream_ptr]);
        stream_ptr += 4;
        chunk_type = LE_16(&buf[stream_ptr]);
        stream_ptr += 2;

        switch (chunk_type) {
        case FLI_256_COLOR:
        case FLI_COLOR:
            /* indicate a palette change to the calling app */
            s->frame.new_palette = 1;

            stream_ptr_after_color_chunk = stream_ptr + chunk_size - 6;
            /* check special case: if this has the internal magic number of
             * 0xAF13, this file is from the Magic Carpet game and uses 6-bit
             * colors even though it reports 256-color chunks in a 0xAF12-type
             * file */
//            if ((chunk_type == FLI_256_COLOR) && (this->magic_number != 0xAF13))
            if (chunk_type == FLI_256_COLOR)
                color_shift = 0;
            else
                color_shift = 2;
            /* set up the palette */
            color_packets = LE_16(&buf[stream_ptr]);
            stream_ptr += 2;
            palette_ptr1 = 0;
            for (i = 0; i < color_packets; i++) {
                /* first byte is how many colors to skip */
                palette_ptr1 += (buf[stream_ptr++] * 4);

                /* next byte indicates how many entries to change */
                color_changes = buf[stream_ptr++];

                /* if there are 0 color changes, there are actually 256 */
                if (color_changes == 0)
                    color_changes = 256;

                for (j = 0; j < color_changes; j++) {

                    /* wrap around, for good measure */
                    if (palette_ptr1 >= 256)
                        palette_ptr1 = 0;

                    r = buf[stream_ptr + 0] << color_shift;
                    g = buf[stream_ptr + 1] << color_shift;
                    b = buf[stream_ptr + 2] << color_shift;
                    s->frame.palette[j] =
                        0xFF000000 | (r << 16) | (g << 8) | b;

                    palette_ptr1++;
                    stream_ptr += 3;
                }
            }

            /* color chunks sometimes have weird 16-bit alignment issues;
             * therefore, take the hardline approach and set the stream_ptr
             * to the value calculate w.r.t. the size specified by the color
             * chunk header */
            stream_ptr = stream_ptr_after_color_chunk;

            /* palette has changed, must update frame */
            update_whole_frame = 1;
            break;

        case FLI_DELTA:
            y_ptr = ghost_y_ptr = 0;
            compressed_lines = LE_16(&buf[stream_ptr]);
            stream_ptr += 2;
            while (compressed_lines > 0) {
                line_packets = LE_16(&buf[stream_ptr]);
                stream_ptr += 2;
                if (line_packets < 0) {
                    line_packets = -line_packets;
//                    y_ptr += (line_packets * this->yuv_planes.row_width);
                    ghost_y_ptr += (line_packets * s->frame.linesize[0]);
                } else {
                    pixel_ptr = y_ptr;
                    ghost_pixel_ptr = ghost_y_ptr;
                    for (i = 0; i < line_packets; i++) {
                        /* account for the skip bytes */
                        pixel_skip = buf[stream_ptr++];
                        pixel_ptr += pixel_skip;
                        ghost_pixel_ptr += pixel_skip;
                        byte_run = buf[stream_ptr++];
                        if (byte_run < 0) {
                            byte_run = -byte_run;
                            palette_ptr1 = (palette_idx1 = buf[stream_ptr++]) * 4;
                            palette_ptr2 = (palette_idx2 = buf[stream_ptr++]) * 4;
                            for (j = 0; j < byte_run; j++) {
                                s->frame.data[0][ghost_pixel_ptr++] = palette_idx1;
                                s->frame.data[0][ghost_pixel_ptr++] = palette_idx2;
                            }
                        } else {
                            for (j = 0; j < byte_run * 2; j++) {
                                palette_ptr1 = (palette_idx1 = buf[stream_ptr++]) * 4;
                                s->frame.data[0][ghost_pixel_ptr++] = palette_idx1;
                            }
                        }
                    }

//                    y_ptr += this->yuv_planes.row_width;
                    ghost_y_ptr += s->frame.linesize[0];
                    compressed_lines--;
                }
            }
            break;

        case FLI_LC:
            /* line compressed */
            starting_line = LE_16(&buf[stream_ptr]);
            stream_ptr += 2;
//            y_ptr = starting_line * this->yuv_planes.row_width;
            ghost_y_ptr = starting_line * s->frame.linesize[0];

            compressed_lines = LE_16(&buf[stream_ptr]);
            stream_ptr += 2;
            while (compressed_lines > 0) {
                pixel_ptr = y_ptr;
                ghost_pixel_ptr = ghost_y_ptr;
                line_packets = buf[stream_ptr++];
                if (line_packets > 0) {
                    for (i = 0; i < line_packets; i++) {
                        /* account for the skip bytes */
                        pixel_skip = buf[stream_ptr++];
                        pixel_ptr += pixel_skip;
                        ghost_pixel_ptr += pixel_skip;
                        byte_run = buf[stream_ptr++];
                        if (byte_run > 0) {
                            for (j = 0; j < byte_run; j++) {
                                palette_ptr1 = (palette_idx1 = buf[stream_ptr++]) * 4;
                                s->frame.data[0][ghost_pixel_ptr++] = palette_idx1;
                            }
                        } else {
                            byte_run = -byte_run;
                            palette_ptr1 = (palette_idx1 = buf[stream_ptr++]) * 4;
                            for (j = 0; j < byte_run; j++) {
                                s->frame.data[0][ghost_pixel_ptr++] = palette_idx1;
                            }
                        }
                    }
                }

//                y_ptr += this->yuv_planes.row_width;
                ghost_y_ptr += s->frame.linesize[0];
                compressed_lines--;
            }
            break;

        case FLI_BLACK:
            /* set the whole frame to color 0 (which is usually black) by
             * clearing the ghost image and trigger a full frame update */
            memset(s->frame.data[0], 0,
                s->frame.linesize[0] * s->height * sizeof(unsigned char));
            update_whole_frame = 1;
            break;

        case FLI_BRUN:
            /* Byte run compression: This chunk type only occurs in the first
             * FLI frame and it will update the entire frame. Take a lazy
             * approach and update only the ghost image. Then let the ghost
             * image updater at the end of the decoder handle the rest. */
            update_whole_frame = 1;
            ghost_y_ptr = 0;
            for (lines = 0; lines < s->height; lines++) {
                ghost_pixel_ptr = ghost_y_ptr;
                /* disregard the line packets; instead, iterate through all
                 * pixels on a row */
                stream_ptr++;
                pixel_countdown = s->width;
                while (pixel_countdown > 0) {
                    byte_run = buf[stream_ptr++];
                    if (byte_run > 0) {
                        palette_ptr1 = (palette_idx1 = buf[stream_ptr++]) * 4;
                        for (j = 0; j < byte_run; j++) {
                            s->frame.data[0][ghost_pixel_ptr++] = palette_idx1;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                printf ("fli warning: pixel_countdown < 0 (%d)\n",
                                    pixel_countdown);
                        }
                    } else {  /* copy bytes if byte_run < 0 */
                        byte_run = -byte_run;
                        for (j = 0; j < byte_run; j++) {
                            palette_ptr1 = (palette_idx1 = buf[stream_ptr++]) * 4;
                            s->frame.data[0][ghost_pixel_ptr++] = palette_idx1;
                            pixel_countdown--;
                            if (pixel_countdown < 0)
                                printf ("fli warning: pixel_countdown < 0 (%d)\n",
                                    pixel_countdown);
                        }
                    }
                }

                ghost_y_ptr += s->frame.linesize[0];
            }
            break;

        case FLI_COPY:
            /* copy the chunk (uncompressed frame) to the ghost image and
             * schedule the whole frame to be updated */
            if (chunk_size - 6 > s->width * s->height) {
                printf(
                "FLI: in chunk FLI_COPY : source data (%d bytes) bigger than" \
                " image, skipping chunk\n",
                chunk_size - 6);
                break;
            } else
                memcpy(s->frame.data[0], &buf[stream_ptr], chunk_size - 6);
            stream_ptr += chunk_size - 6;
            update_whole_frame = 1;
            break;

        case FLI_MINI:
            /* some sort of a thumbnail? disregard this chunk... */
            stream_ptr += chunk_size - 6;
            break;

        default:
            printf ("FLI: Unrecognized chunk type: %d\n", chunk_type);
            break;
        }

        frame_size -= chunk_size;
        num_chunks--;
    }

#if 0
    if (update_whole_frame) {

        pixel_ptr = ghost_pixel_ptr = 0;
        for (lines = 0; lines < this->height; lines++) {
            for (x = 0; x < this->width; x++) {
                palette_ptr1 = this->ghost_image[ghost_pixel_ptr++] * 4;
                this->yuv_planes.y[pixel_ptr] = this->yuv_palette[palette_ptr1 + 0];
                this->yuv_planes.u[pixel_ptr] = this->yuv_palette[palette_ptr1 + 1];
                this->yuv_planes.v[pixel_ptr] = this->yuv_palette[palette_ptr1 + 2];
                pixel_ptr++;
            }
        }
    }
#endif

    /* by the end of the chunk, the stream ptr should equal the frame
     * size (minus 1, possibly); if it doesn't, issue a warning */
    if ((stream_ptr != buf_size) && (stream_ptr != buf_size - 1))
        printf ("  warning: processed FLI chunk where chunk size = %d\n" \
        "  and final chunk ptr = %d\n",
            buf_size, stream_ptr);

    *data_size=sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    return buf_size;
}

static int flic_decode_end(AVCodecContext *avctx)
{
    FlicDecodeContext *s = avctx->priv_data;

    avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec flic_decoder = {
    "flic",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FLIC,
    sizeof(FlicDecodeContext),
    flic_decode_init,
    NULL,
    flic_decode_end,
    flic_decode_frame,
    CODEC_CAP_DR1,
    NULL
};


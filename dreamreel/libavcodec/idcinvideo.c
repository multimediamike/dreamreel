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
 */

/**
 * @file idcinvideo.c
 * Id CIN Video Decoder by Dr. Tim Ferguson. For more information about
 * the Id CIN format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 *
 * This decoder supports the PAL8, RGB555, RGB565, RGBA32, and YUV444P pixel
 * formats. To select the desired pixel format, set AVCodecContext.pix_fmt
 * to a particular format before initializing with avcodec_open(). If pix_fmt
 * is not one of the approved formats, RGBA32 is the default selected pixel
 * format.
 *
 * This decoder needs the 65536-byte chunk from the original Id CIN file
 * which comprises 256 256-element histograms for construction Huffman
 * decoding tables. Before calling avcodec_open(), allocate 65536 bytes
 * for AVCodecContext.extradata, copy the histogram data in, and set 
 * .extradata_size to 65536.
 *
 * This decoder supports both direct rendering (CODEC_CAP_DR1) and slice
 * dispatch (CODEC_CAP_DRAW_HORIZ_BAND) for all pixel formats.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "bswap.h"

typedef struct IdcinDecodeContext {
    AVCodecContext *avctx;
    int width, height;
    void *palette;
} IdcinDecodeContext;

/**************************************************************************
 * idcinvideo specific decode functions
 *************************************************************************/

#define HUF_TOKENS 256

typedef struct
{
  long rate;
  long width;
  long channels;
} wavinfo_t;

typedef struct
{
    int count;
    unsigned char used;
    int children[2];
} hnode_t;

static hnode_t huff_nodes[256][HUF_TOKENS*2];
static int num_huff_nodes[256];

/*
 *  Decodes input Huffman data using the Huffman table.
 */
static void huff_decode(IdcinDecodeContext *s, AVFrame *frame, uint8_t *buf,
    int buf_size)
{
    hnode_t *hnodes;
    int prev;
    unsigned char v = 0;
    int bit_pos, node_num, dat_pos;
    int address, x, y;;

    prev = bit_pos = dat_pos = 0;
    for (y = 0; y < s->height; y++) {
        address = y * frame->linesize[0];
        for (x = 0; x < s->width; x++) {

            node_num = num_huff_nodes[prev];
            hnodes = huff_nodes[prev];

            /* naive Huffman tree traversal */
            while(node_num >= HUF_TOKENS) {
                if(!bit_pos) {
                    if(dat_pos > buf_size) {
                        printf("Huffman decode error.\n");
                        return;
                    }
                    bit_pos = 8;
                    v = buf[dat_pos++];
                }

                node_num = hnodes[node_num].children[v & 0x01];
                v = v >> 1;
                bit_pos--;
            }

            frame->data[0][address] = node_num;
            prev = node_num;
        }
    }

#if 0
    for(i = 0; i < (s->width * s->height); i++) {
        node_num = num_huff_nodes[prev];
        hnodes = huff_nodes[prev];

        while(node_num >= HUF_TOKENS) {
            if(!bit_pos) {
                if(dat_pos > buf_size) {
                    printf("Huffman decode error.\n");
                    return;
                }
                bit_pos = 8;
                v = buf[dat_pos++];
            }

            node_num = hnodes[node_num].children[v & 0x01];
            v = v >> 1;
            bit_pos--;
        }

/*
        this->yuv_planes.y[plane_ptr] = this->yuv_palette[node_num * 4 + 0];
        this->yuv_planes.u[plane_ptr] = this->yuv_palette[node_num * 4 + 1];
        this->yuv_planes.v[plane_ptr] = this->yuv_palette[node_num * 4 + 2];
*/
        plane_ptr++;

        prev = node_num;
    }
#endif
}

/*
 *  Find the lowest probability node in a Huffman table, and mark it as
 *  being assigned to a higher probability.
 *  Returns the node index of the lowest unused node, or -1 if all nodes
 *  are used.
 */
static int huff_smallest_node(hnode_t *hnodes, int num_hnodes)
{
    int i;
    int best, best_node;

    best = 99999999;
    best_node = -1;
    for(i = 0; i < num_hnodes; i++) {
        if(hnodes[i].used)
            continue;
        if(!hnodes[i].count)
            continue;
        if(hnodes[i].count < best) {
            best = hnodes[i].count;
            best_node = i;
        }
    }

    if(best_node == -1)
        return -1;
    hnodes[best_node].used = 1;
    return best_node;
}

/*
 *  Build the Huffman tree using the generated/loaded probabilities histogram.
 *
 *  On completion:
 *   huff_nodes[prev][i < HUF_TOKENS] - are the nodes at the base of the tree.
 *   huff_nodes[prev][i >= HUF_TOKENS] - are used to construct the tree.
 *   num_huff_nodes[prev] - contains the index to the root node of the tree.
 *     That is: huff_nodes[prev][num_huff_nodes[prev]] is the root node.
 */
static void huff_build_tree(int prev)
{
    hnode_t *node, *hnodes;
    int num_hnodes, i;

    num_hnodes = HUF_TOKENS;
    hnodes = huff_nodes[prev];
    for(i = 0; i < HUF_TOKENS * 2; i++)
        hnodes[i].used = 0;

    while (1) {
        node = &hnodes[num_hnodes];             /* next free node */

        /* pick two lowest counts */
        node->children[0] = huff_smallest_node(hnodes, num_hnodes);
        if(node->children[0] == -1)
            break;      /* reached the root node */

        node->children[1] = huff_smallest_node(hnodes, num_hnodes);
        if(node->children[1] == -1)
            break;      /* reached the root node */

        /* combine nodes probability for new node */
        node->count = hnodes[node->children[0]].count +
            hnodes[node->children[1]].count;
        num_hnodes++;
    }

    num_huff_nodes[prev] = num_hnodes - 1;
}

/**************************************************************************
 * ffmpeg API functions
 *************************************************************************/

static int idcin_decode_init(AVCodecContext *avctx)
{
    IdcinDecodeContext *s = avctx->priv_data;
    int i, j;
    unsigned char *histograms;
    int histogram_index = 0;

    s->avctx = avctx;
    s->width = avctx->width;
    s->height = avctx->height;
    avctx->has_b_frames = 0;

    /* check if the requested pixelformat is on the "approved" list */
    if ((avctx->pix_fmt != PIX_FMT_PAL8) &&
        (avctx->pix_fmt != PIX_FMT_RGB555) &&
        (avctx->pix_fmt != PIX_FMT_RGB565) &&
        (avctx->pix_fmt != PIX_FMT_RGBA32) &&
        (avctx->pix_fmt != PIX_FMT_YUV444P))
        avctx->pix_fmt = PIX_FMT_RGBA32;  /* default pixelformat */

    /* check if the histogram data is present */
    if ((avctx->extradata_size != 65536) || (!avctx->extradata))
        return -1;

    /* initialize the Huffman tables */
    histograms = (unsigned char *)avctx->extradata;
    for (i = 0; i < 256; i++) {
        for(j = 0; j < HUF_TOKENS; j++)
            huff_nodes[i][j].count = histograms[histogram_index++];
        huff_build_tree(i);
    }

    /* allocate the palette */
    if ((avctx->pix_fmt == PIX_FMT_RGB555) ||
        (avctx->pix_fmt == PIX_FMT_RGB565))
        s->palette = av_malloc(256 * 2);  /* 2 bytes per each of 256 entries */
    else if ((avctx->pix_fmt == PIX_FMT_RGBA32) ||
             (avctx->pix_fmt == PIX_FMT_YUV444P))
        s->palette = av_malloc(256 * 4);  /* 4 bytes per each of 256 entries */

    return 0;
}

static int idcin_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    IdcinDecodeContext *s=avctx->priv_data;
    AVFrame frame;

    if (avctx->get_buffer(avctx, &frame) < 0) {
        fprintf(stderr, "get_buffer() failed\n");
        return -1;
    }

    huff_decode(s, &frame, buf, buf_size);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = frame;

    avctx->release_buffer(avctx, &frame);

    return buf_size;
}

static int idcin_decode_end(AVCodecContext *avctx)
{
    IdcinDecodeContext *s = avctx->priv_data;

    av_free(s->palette);

    return 0;
}

AVCodec idcin_decoder = {
    "idcin",
    CODEC_TYPE_VIDEO,
    CODEC_ID_IDCIN,
    sizeof(IdcinDecodeContext),
    idcin_decode_init,
    NULL,
    idcin_decode_end,
    idcin_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DRAW_HORIZ_BAND,
    NULL
};


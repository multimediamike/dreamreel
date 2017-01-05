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
 * $Id: buffer_types.c,v 1.56 2003/03/23 17:12:30 holstsn Exp $
 *
 *
 * contents:
 *
 * buffer types management. 
 * convert FOURCC and audioformattag to BUF_xxx defines
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "dreamreel.h"
#include "buffer.h"
#include "bswap.h"

/* FOURCC will be manipulated using machine endian */
#ifdef WORDS_BIGENDIAN
#define meFOURCC( ch0, ch1, ch2, ch3 )              \
        ( (uint32_t)(unsigned char)(ch3) |          \
        ( (uint32_t)(unsigned char)(ch2) << 8 ) |   \
        ( (uint32_t)(unsigned char)(ch1) << 16 ) |  \
        ( (uint32_t)(unsigned char)(ch0) << 24 ) )
#else
#define meFOURCC( ch0, ch1, ch2, ch3 )              \
        ( (uint32_t)(unsigned char)(ch0) |          \
        ( (uint32_t)(unsigned char)(ch1) << 8 ) |   \
        ( (uint32_t)(unsigned char)(ch2) << 16 ) |  \
        ( (uint32_t)(unsigned char)(ch3) << 24 ) )
#endif


typedef struct video_db_s {
   uint32_t fourcc[20];
   uint32_t buf_type;
   char *name;
} video_db_t;

typedef struct audio_db_s {
   uint32_t formattag[10];
   uint32_t buf_type;
   char *name;
} audio_db_t;


static video_db_t video_db[] = {
{
  {
    meFOURCC('m', 'p', 'e', 'g'),
    meFOURCC('P', 'I', 'M', '1'),
    0
  },
  BUF_VIDEO_MPEG,
  "MPEG 1/2"
},
{
  {
    meFOURCC('D', 'I', 'V', 'X'),
    meFOURCC('d', 'i', 'v', 'x'),
    meFOURCC('D', 'i', 'v', 'x'),
    meFOURCC('D', 'i', 'v', 'X'),
    meFOURCC('M', 'P', '4', 'S'),
    meFOURCC('m', 'p', '4', 'v'),
    meFOURCC('M', '4', 'S', '2'),
    meFOURCC('m', '4', 's', '2'),
    0
  },
  BUF_VIDEO_MPEG4,
  "ISO-MPEG4/OpenDivx format"
},
{
  {
    meFOURCC('X', 'V', 'I', 'D'),
    0
  },
  BUF_VIDEO_XVID,
  "XviD format"
},
{
  {
    meFOURCC('D', 'X', '5', '0'),
    0
  },
  BUF_VIDEO_DIVX5,
  "DivX 5 format"
},
{
  {
    meFOURCC('c', 'v', 'i', 'd'),
    0
  },
  BUF_VIDEO_CINEPAK,
  "Cinepak format"
},
{
  {
    meFOURCC('S', 'V', 'Q', '1'),
    meFOURCC('s', 'v', 'q', '1'),
    meFOURCC('s', 'v', 'q', 'i'),
    0
  },
  BUF_VIDEO_SORENSON_V1,
  "Sorenson Video 1"
},
{
  {
    meFOURCC('S', 'V', 'Q', '3'),
    meFOURCC('s', 'v', 'q', '3'),
    0
  },
  BUF_VIDEO_SORENSON_V3,
  "Sorenson Video 3"
},
{
  {
    meFOURCC('M', 'P', '4', '1'),
    meFOURCC('m', 'p', '4', '1'),
    meFOURCC('M', 'P', 'G', '4'),
    meFOURCC('m', 'p', 'g', '4'),
    0
  },
  BUF_VIDEO_MSMPEG4_V1,
  "Microsoft MPEG-4 format v1"
},
{
  {
    meFOURCC('M', 'P', '4', '1'),
    meFOURCC('m', 'p', '4', '1'),
    meFOURCC('M', 'P', '4', '2'),
    meFOURCC('m', 'p', '4', '2'),
    meFOURCC('D', 'I', 'V', '2'),
    meFOURCC('d', 'i', 'v', '2'),
    0
  },
  BUF_VIDEO_MSMPEG4_V2,
  "Microsoft MPEG-4 format v2"
},
{
  {
    meFOURCC('M', 'P', '4', '3'),
    meFOURCC('m', 'p', '4', '3'),
    meFOURCC('D', 'I', 'V', '3'),
    meFOURCC('d', 'i', 'v', '3'),
    meFOURCC('D', 'I', 'V', '4'),
    meFOURCC('d', 'i', 'v', '4'),
    meFOURCC('D', 'I', 'V', '5'),
    meFOURCC('d', 'i', 'v', '5'),
    meFOURCC('D', 'I', 'V', '6'),
    meFOURCC('d', 'i', 'v', '6'),
    meFOURCC('A', 'P', '4', '1'),
    meFOURCC('M', 'P', 'G', '3'),
    meFOURCC('C', 'O', 'L', '1'),
    0
  },
  BUF_VIDEO_MSMPEG4_V3,
  "Microsoft MPEG-4 format v3"
},
{
  {
    meFOURCC('3', 'I', 'V', '1'),
    0
  },
  BUF_VIDEO_3IVX,
  "3ivx MPEG-4"
},
{
  {
    meFOURCC('d', 'm', 'b', '1'),
    meFOURCC('M', 'J', 'P', 'G'),
    meFOURCC('m', 'j', 'p', 'a'),
    meFOURCC('m', 'j', 'p', 'b'),
    0
  },
  BUF_VIDEO_MJPEG,
  "motion jpeg format"
},
{
  {
    meFOURCC('I', 'V', '5', '0'),
    meFOURCC('i', 'v', '5', '0'),
    0
  },
  BUF_VIDEO_IV50,
  "Indeo Video 5.0 format"
},
{
  {
    meFOURCC('I', 'V', '4', '1'),
    meFOURCC('i', 'v', '4', '1'),
    0
  },
  BUF_VIDEO_IV41,
  "Indeo Video 4.1 format"
},
{
  {
    meFOURCC('I', 'V', '3', '2'),
    meFOURCC('i', 'v', '3', '2'),
    0
  },
  BUF_VIDEO_IV32,
  "Indeo Video 3.2 format"
},
{
  {
    meFOURCC('I', 'V', '3', '1'),
    meFOURCC('i', 'v', '3', '1'),
    0
  },
  BUF_VIDEO_IV31,
  "Indeo Video 3.1 format"
},
{
  {
    meFOURCC('V', 'C', 'R', '1'),
    0
  },
  BUF_VIDEO_ATIVCR1,
  "ATI VCR1 format"
},
{
  {
    meFOURCC('V', 'C', 'R', '2'),
    0
  },
  BUF_VIDEO_ATIVCR2,
  "ATI VCR2 format"
},
{
  {
    meFOURCC('I', '2', '6', '3'),
    meFOURCC('i', '2', '6', '3'),
    meFOURCC('V', 'I', 'V', 'O'),
    meFOURCC('v', 'i', 'v', 'o'),
    meFOURCC('v', 'i', 'v', '1'),
    0
  },
  BUF_VIDEO_I263,
  "I263 format"
},
{
  {
    meFOURCC('D','I','B',' '),  /* device-independent bitmap */
    meFOURCC('r','a','w',' '),
    0
  },
  BUF_VIDEO_RGB,
  "Raw RGB"
},
{
  { 
    /* is this right? copied from demux_qt:
    else if (!strncasecmp (video, "yuv2", 4))
    this->video_type = BUF_VIDEO_YUY2;
    */
    meFOURCC('y','u','v','2'),
    0
  },
  BUF_VIDEO_YUY2,
  ""
},
{
  {
    meFOURCC('j','p','e','g'),
    meFOURCC('J','F','I','F'),
    0
  },
  BUF_VIDEO_JPEG,
  "jpeg"
},
{
  {
    meFOURCC('W','M','V','1'),
    0
  },
  BUF_VIDEO_WMV7,
  "Windows Media Video 7"
},
{
  {
    meFOURCC('W','M','V','2'),
    0
  },
  BUF_VIDEO_WMV8,
  "Windows Media Video 8"
},
{
  {
    meFOURCC('W','M','V','3'),
    0
  },
  BUF_VIDEO_WMV9,
  "Windows Media Video 9"
},
{
  {
    meFOURCC('c','r','a','m'),
    meFOURCC('C','R','A','M'),
    meFOURCC('M','S','V','C'),
    meFOURCC('m','s','v','c'),
    meFOURCC('W','H','A','M'),
    meFOURCC('w','h','a','m'),
    0
  },
  BUF_VIDEO_MSVC,
  "Microsoft Video 1"
},
{
  {
    meFOURCC('D','V','S','D'),
    meFOURCC('d','v','s','d'),
    meFOURCC('d','v','c','p'),
    0
  },
  BUF_VIDEO_DV,
  "Sony Digital Video (DV)"
},
{
  {
    meFOURCC('V','P','3','0'),
    meFOURCC('v','p','3','0'),
    meFOURCC('V','P','3','1'),
    meFOURCC('v','p','3','1'),
    0
  },
  BUF_VIDEO_VP31,
  "On2 VP3.1 Codec"
},
{
  {
    meFOURCC('H', '2', '6', '3'),
    meFOURCC('h', '2', '6', '3'),
    meFOURCC('U', '2', '6', '3'),
    0
  },
  BUF_VIDEO_H263,
  "H263 format"
},
{
  {
    meFOURCC('c', 'y', 'u', 'v'),
    meFOURCC('C', 'Y', 'U', 'V'),
    0
  },
  BUF_VIDEO_CYUV,
  "Creative YUV format"
},
{
  {
    meFOURCC('s', 'm', 'c', ' '),
    0
  },
  BUF_VIDEO_SMC,
  "Apple Quicktime Graphics (SMC)"
},
{
  {
    meFOURCC('r', 'p', 'z', 'a'),
    meFOURCC('a', 'z', 'p', 'r'),
    0
  },
  BUF_VIDEO_RPZA,
  "Apple Quicktime Video (RPZA)"
},
{
  {
    meFOURCC('r', 'l', 'e', ' '),
    0
  },
  BUF_VIDEO_QTRLE,
  "Apple Quicktime Animation (RLE)"
},
{
  {
    1, 2, 0  /* MS RLE format identifiers */
  },
  BUF_VIDEO_MSRLE,
  "Microsoft RLE"
},
{
  {
    meFOURCC('D', 'U', 'C', 'K'),
    0
  },
  BUF_VIDEO_DUCKTM1,
  "Duck Truemotion v1"
},
{
  {
    meFOURCC('M', 'S', 'S', '1'),
    0
  },
  BUF_VIDEO_MSS1,
  "Windows Screen Video"
},
{
  {
    meFOURCC('P', 'G', 'V', 'V'),
    0
  },
  BUF_VIDEO_PGVV,
  "Radius Studio Codec"
},
{
  {
    meFOURCC('Z', 'y', 'G', 'o'),
    0
  },
  BUF_VIDEO_ZYGO,
  "ZyGo Video"
},
{
  {
    meFOURCC('t', 's', 'c', 'c'),
    0
  },
  BUF_VIDEO_TSCC,
  "TechSmith Screen Capture Codec"
},
{
  {
    meFOURCC('Y', 'V', 'U', '9'),
    0
  },
  BUF_VIDEO_YVU9,
  "Raw YVU9 Planar Data"
},
{
  {
    meFOURCC('G', 'R', 'E', 'Y'),
    0
  },
  BUF_VIDEO_GREY,
  "Raw greyscale"
},
{
  {
    meFOURCC('X', 'x', 'a', 'n'),
    meFOURCC('X', 'X', 'A', 'N'),
    meFOURCC('x', 'x', 'a', 'n'),
    0
  },
  BUF_VIDEO_XXAN,
  "Wing Commander IV Video Codec"
},
{
  {
    meFOURCC('S', 'E', 'G', 'A'),
    meFOURCC('s', 'e', 'g', 'a'),
    0
  },
  BUF_VIDEO_SEGA,
  "Cinepak for Sega"
},
{
  {
    meFOURCC('m', 'v', 'i', '2'),
    meFOURCC('M', 'V', 'I', '2'),
    0
  },
  BUF_VIDEO_MVI2,
  "Motion Pixels"
},
{
  {
    meFOURCC('u', 'c', 'o', 'd'),
    meFOURCC('U', 'C', 'O', 'D'),
    0
  },
  BUF_VIDEO_UCOD,
  "ClearVideo"
},
{
  {
    meFOURCC('R', 'V', '1', '0'),
    0
  },
  BUF_VIDEO_RV10,
  "Real Video 1.0"
},
{
  {
    meFOURCC('R', 'V', '2', '0'),
    0
  },
  BUF_VIDEO_RV20,
  "Real Video 2.0"
},
{
  {
    meFOURCC('R', 'V', '3', '0'),
    0
  },
  BUF_VIDEO_RV30,
  "Real Video 3.0"
},
{
  {
    meFOURCC('R', 'V', '4', '0'),
    0
  },
  BUF_VIDEO_RV40,
  "Real Video 4.0"
},
{
  {
    meFOURCC('H', 'F', 'Y', 'U'),
    0,
  },
  BUF_VIDEO_HUFFYUV,
  "HuffYUV"
},
{
  {
    meFOURCC('I', 'M', 'G', ' '),
    0,
  },
  BUF_VIDEO_IMAGE,
  "Image"
},

{ { 0 }, 0, "last entry" }
};


static audio_db_t audio_db[] = {
{
  {
    0x2000, 0
  },
  BUF_AUDIO_A52,
  "AC3"
},
{
  {
    0x50, 0x55,
    meFOURCC('.','m','p','3'),
    meFOURCC('m', 's', 0, 0x55),
    0
  },
  BUF_AUDIO_MPEG,
  "MPEG layer 2/3"
},
{
  {
    meFOURCC('t','w','o','s'),
    0
  },
  BUF_AUDIO_LPCM_BE,
  "Uncompressed PCM big endian"
},
{
  {
    0x01,
    meFOURCC('r','a','w',' '),
    meFOURCC('s','o','w','t'),
    0
  },
  BUF_AUDIO_LPCM_LE,
  "Uncompressed PCM little endian"
},
{
  {
    0x160, 0
  },
  BUF_AUDIO_WMAV1,
  "Windows Media Audio v1"
},
{
  {
    0x161, 0
  },
  BUF_AUDIO_WMAV2,
  "Windows Media Audio v2"
},
{
  {
    0x162, 0
  },
  BUF_AUDIO_WMAV3,
  "Windows Media Audio v3"
},
{
  {
    0xA, 0
  },
  BUF_AUDIO_WMAV,
  "Windows Media Audio Voice"
},
{
  {
    0
  },
  BUF_AUDIO_DTS,
  "DTS"
},
{
  {
    0x02, 
    meFOURCC('m', 's', 0, 0x02),
    0
  },
  BUF_AUDIO_MSADPCM,
  "MS ADPCM"
},
{
  {
    0x11,
    meFOURCC('m', 's', 0, 0x11),
    0
  },
  BUF_AUDIO_MSIMAADPCM,
  "MS IMA ADPCM"
},
{
  {
    0x31, 0x32, 0
  },
  BUF_AUDIO_MSGSM,
  "MS GSM"
},
{
  {                                  
    /* these formattags are used by Vorbis ACM encoder and
       supported by NanDub, a variant of VirtualDub. */
    0x674f, 0x676f, 0x6750, 0x6770, 0x6751, 0x6771,
    meFOURCC('O','g','g','S'),
    meFOURCC('O','g','g','V'),
    0
  },
  BUF_AUDIO_VORBIS,
  "OggVorbis Audio"
},
{
  {
    0x401, 0
  },
  BUF_AUDIO_IMC,
  "Intel Music Coder"
},
{
  {
    0x1101, 0x1102, 0x1103, 0x1104, 0
  },
  BUF_AUDIO_LH,
  "Lernout & Hauspie"
},
{
  {
    0x75, 0
  },
  BUF_AUDIO_VOXWARE,
  "Voxware Metasound"
},
{
  {
    0x130, 0
  },
  BUF_AUDIO_ACELPNET,
  "ACELP.net"
},
{
  {
    0x111, 0x112, 0
  },
  BUF_AUDIO_VIVOG723,
  "Vivo G.723/Siren Audio Codec"
},
{
  {
    0x61, 0
  },
  BUF_AUDIO_DK4ADPCM,
  "Duck DK4 ADPCM (rogue format number)"
},
{
  {
    0x62, 0
  },
  BUF_AUDIO_DK3ADPCM,
  "Duck DK3 ADPCM (rogue format number)"
},
{
  {
    meFOURCC('i', 'm', 'a', '4'),
    0
  },
  BUF_AUDIO_QTIMAADPCM,
  "QT IMA ADPCM"
},
{
  {
    meFOURCC('m', 'a', 'c', '3'),
    meFOURCC('M', 'A', 'C', '3'),
    0
  },
  BUF_AUDIO_MAC3,
  "Apple MACE 3:1 Audio"
},
{
  {
    meFOURCC('m', 'a', 'c', '6'),
    meFOURCC('M', 'A', 'C', '6'),
    0
  },
  BUF_AUDIO_MAC6,
  "Apple MACE 6:1 Audio"
},
{
  {
    meFOURCC('Q', 'D', 'M', 'C'),
    0
  },
  BUF_AUDIO_QDESIGN1,
  "QDesign Audio v1"
},
{
  {
    meFOURCC('Q', 'D', 'M', '2'),
    0
  },
  BUF_AUDIO_QDESIGN2,
  "QDesign Audio v2"
},
{
  {
    meFOURCC('m', 'p', '4', 'a'),
    meFOURCC('M', 'P', '4', 'A'),
    0
  },
  BUF_AUDIO_AAC,
  "Advanced Audio Coding (MPEG-4 AAC)"
},
{
  {
    meFOURCC('d', 'n', 'e', 't'),
    0
  },
  BUF_AUDIO_DNET,
  "RealAudio DNET"
},
{
  {
    meFOURCC('s', 'i', 'p', 'r'),
    0
  },
  BUF_AUDIO_SIPRO,
  "RealAudio SIPRO"
},
{
  {
    meFOURCC('c', 'o', 'o', 'k'),
    0
  },
  BUF_AUDIO_COOK,
  "RealAudio COOK"
},
{
  {
    meFOURCC('a', 't', 'r', 'c'),
    0
  },
  BUF_AUDIO_ATRK,
  "RealAudio ATRK"
},
{
  {
    meFOURCC('Q', 'c', 'l', 'p'),
    0
  },
  BUF_AUDIO_QCLP,
  "Qualcomm PureVoice"
},
{
  {
    0x7,
    meFOURCC('u', 'l', 'a', 'w'),
    0
  },
  BUF_AUDIO_MULAW,
  "mu-law logarithmic PCM"
},
{
  {
    0x6,
    meFOURCC('a', 'l', 'a', 'w'),
    0
  },
  BUF_AUDIO_ALAW,
  "A-law logarithmic PCM"
},
{
  {
    meFOURCC('a', 'g', 's', 'm'),
    0
  },
  BUF_AUDIO_GSM610,
  "GSM 6.10"
},
{
  {
    0
  },
  BUF_AUDIO_FLAC,
  "Free Lossless Audio Codec (FLAC)"
},
{
  {
    0
  },
  BUF_AUDIO_DV,
  "DV Audio"
},
{ { 0 }, 0, "last entry" }
};


uint32_t fourcc_to_buf_video( uint32_t fourcc_int ) {
int i, j;
static uint32_t cached_fourcc=0;
static uint32_t cached_buf_type=0;

  if( fourcc_int == cached_fourcc )
    return cached_buf_type;
    
  for( i = 0; video_db[i].buf_type; i++ ) {
    for( j = 0; video_db[i].fourcc[j]; j++ ) {
      if( fourcc_int == video_db[i].fourcc[j] ) {
        cached_fourcc = fourcc_int;
        cached_buf_type = video_db[i].buf_type;
        return video_db[i].buf_type;
      }
    }
  }
  return 0;
}

char * buf_video_name( uint32_t buf_type ) {
int i;
  
  buf_type &= 0xffff0000;
  
  for( i = 0; video_db[i].buf_type; i++ ) {
    if( buf_type == video_db[i].buf_type ) {
        return video_db[i].name;
    }
  }

  return "";
}

uint32_t formattag_to_buf_audio( uint32_t formattag ) {
int i, j;
static uint16_t cached_formattag=0;
static uint32_t cached_buf_type=0;

  if( formattag == cached_formattag )
    return cached_buf_type;
    
  for( i = 0; audio_db[i].buf_type; i++ ) {
    for( j = 0; audio_db[i].formattag[j]; j++ ) {
      if( formattag == audio_db[i].formattag[j] ) {
        cached_formattag = formattag;
        cached_buf_type = audio_db[i].buf_type;
        return audio_db[i].buf_type;
      }
    }
  }
  return 0;
}

char * buf_audio_name( uint32_t buf_type ) {
int i;
  
  buf_type &= 0xffff0000;
  
  for( i = 0; audio_db[i].buf_type; i++ ) {
    if( buf_type == audio_db[i].buf_type ) {
        return audio_db[i].name;
    }
  }

  return "";
}

void xine_bmiheader_le2me( xine_bmiheader *bih ) {
  /* OBS: fourcc must be read using machine endianness
   *      so don't play with biCompression here!
   */
  
  bih->biSize = le2me_32(bih->biSize);
  bih->biWidth = le2me_32(bih->biWidth);
  bih->biHeight = le2me_32(bih->biHeight);
  bih->biPlanes = le2me_16(bih->biPlanes);
  bih->biBitCount = le2me_16(bih->biBitCount);
  bih->biSizeImage = le2me_32(bih->biSizeImage);
  bih->biXPelsPerMeter = le2me_32(bih->biXPelsPerMeter);
  bih->biYPelsPerMeter = le2me_32(bih->biYPelsPerMeter);
  bih->biClrUsed = le2me_32(bih->biClrUsed);
  bih->biClrImportant = le2me_32(bih->biClrImportant);
}

void xine_waveformatex_le2me( xine_waveformatex *wavex ) {
  
  wavex->wFormatTag = le2me_16(wavex->wFormatTag);
  wavex->nChannels = le2me_16(wavex->nChannels);
  wavex->nSamplesPerSec = le2me_32(wavex->nSamplesPerSec);
  wavex->nAvgBytesPerSec = le2me_32(wavex->nAvgBytesPerSec);
  wavex->nBlockAlign = le2me_16(wavex->nBlockAlign);
  wavex->wBitsPerSample = le2me_16(wavex->wBitsPerSample);
  wavex->cbSize = le2me_16(wavex->cbSize);
}

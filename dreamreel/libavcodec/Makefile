# Dreamreel
# Makefile for ffmpeg's A/V decoders
#
# $Id$

LIB = libavcodec.a

KOS_LOCAL_CFLAGS = -I. -I../core -DHAVE_AV_CONFIG_H

OBJS = \
	common.o \
	cyuv.o \
	dsputil.o \
	flic.o \
	idcinvideo.o \
	imgconvert.o \
	jrevdct.o \
	mem.o \
	simple_idct.o \
	utils.o

all: $(OBJS)
	$(KOS_AR) rcs $(LIB) *.o

include $(KOS_BASE)/Makefile.rules

clean:
	rm -f *.o $(LIB)


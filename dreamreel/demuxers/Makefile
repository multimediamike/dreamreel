# Dreamreel
# Makefile for demuxers
#
# $Id$

LIB = libdemuxers.a

KOS_LOCAL_CFLAGS = -I. -I../core -I../libavcodec

OBJS = \
	demux_film.o \
	demux_fli.o \
	demux_idcin.o \
	demux_yuv4mpeg2.o 

all: $(OBJS)
	$(KOS_AR) rcs $(LIB) *.o

include $(KOS_BASE)/Makefile.rules

clean:
	rm -f *.o $(LIB)


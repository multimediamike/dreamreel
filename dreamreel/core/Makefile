# Dreamreel
# Makefile for the core engine
#
# $Id$

LIB = libcore.a

KOS_LOCAL_CFLAGS = -I. -I.. -I../libavcodec

OBJS = \
	audio_decoder.o \
	buffer_types.o \
	demux.o \
	dreamreel.o \
	gui.o \
	input_cdfile.o \
	metronom.o \
	video_decoder.o \
	video_out.o 

all: $(OBJS)
	$(KOS_AR) rcs $(LIB) $(OBJS)

include $(KOS_BASE)/Makefile.rules

clean:
	rm -f $(LIB) *.o


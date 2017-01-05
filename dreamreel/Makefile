# KallistiOS 1.1.9
#
#   

all: dreamreel.elf

include $(KOS_BASE)/Makefile.rules

SUBDIRS = core demuxers libavcodec

# strange ordering, but apparently necessary
LIBS = core/libcore.a demuxers/libdemuxers.a core/libcore.a libavcodec/libavcodec.a

DATAOBJS = romdisk_boot.o

clean:
	make -C core clean
	make -C demuxers clean
	make -C libavcodec clean
	-rm -f dreamreel.elf

rm-elf:
	-rm -f dreamreel.elf

dreamreel.elf: subdirs $(DATAOBJS)
	make -C core
	make -C demuxers
	make -C libavcodec
	$(KOS_CC) $(KOS_CFLAGS) $(KOS_LDFLAGS) -o dreamreel.elf $(KOS_START) $(LIBS) $(DATAOBJS) $(OBJEXTRA) $(KOS_LIBS) -lz

romdisk_boot.img:
	$(KOS_GENROMFS) -f romdisk_boot.img -d romdisk_boot -v

romdisk_boot.o: romdisk_boot.img
	$(KOS_BASE)/utils/bin2o/bin2o romdisk_boot.img romdisk_boot romdisk_boot.o

run: dreamreel.elf
	$(KOS_LOADER) dreamreel.elf

dist:
	-rm -f $(OBJS)
	$(KOS_STRIP) dreamreel.elf

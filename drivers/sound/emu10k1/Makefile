# Makefile for Creative Labs EMU10K1 
#
# 12 Apr 2000 Rui Sousa

O_TARGET := emu10k1.o

obj-y :=     audio.o cardmi.o cardmo.o cardwi.o cardwo.o ecard.o \
             emuadxmg.o hwaccess.o irqmgr.o main.o midi.o mixer.o \
             recmgr.o timer.o voicemgr.o
obj-m := $(O_TARGET)

ifdef DEBUG
    EXTRA_CFLAGS += -DEMU10K1_DEBUG
endif

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

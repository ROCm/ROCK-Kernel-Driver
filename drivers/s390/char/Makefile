# Makefile for the S/390 supported character devices
#
# 4 January 2001 Richard Hitt
# Modeled after similar files of Michael Elizabeth Chastain
# Rewritten to use lists instead of if-statements.

O_TARGET	:= s390-char.o

export-objs     :=
list-multi	:= tub3270.o tape390.o

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

tub3270-objs			:= tuball.o tubfs.o tubtty.o \
					tubttyaid.o tubttybld.o tubttyrcl.o \
					tubttyscl.o tubttysiz.o

obj-y += ctrlchar.o
obj-$(CONFIG_3215_CONSOLE)	+= con3215.o
obj-$(CONFIG_HWC)		+= hwc_con.o hwc_rw.o hwc_tty.o
obj-$(CONFIG_3270)		+= tub3270.o

tape-y				:= tape.o
tape-$(CONFIG_S390_TAPE_CHAR)	+= tapechar.o
tape-$(CONFIG_S390_TAPE_BLOCK)	+= tapeblock.o
tape-$(CONFIG_S390_TAPE_3480)	+= tape3480.o tape34xx.o
tape-$(CONFIG_S390_TAPE_3490)	+= tape3490.o tape34xx.o
tape390-objs			:= $(sort $(tape-y))
obj-$(CONFIG_S390_TAPE)		+= tape390.o

# Hand off to Rules.make.

include $(TOPDIR)/Rules.make

tub3270.o:	$(tub3270-objs)
	$(LD) -r -o $@ $(tub3270-objs)

tape390.o:	$(tape390-objs)
	$(LD) -r -o $@ $(tape390-objs)

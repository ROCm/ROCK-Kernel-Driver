#
# Makefile for the Acorn block device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now inherited from the
# parent makefile.
#

USE_STANDARD_AS_RULE := true

L_TARGET	:= acorn-block.a

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

export-objs	:=
list-multi	:= fd1772_mod.o mfmhd_mod.o
fd1772_mod-objs	:= fd1772.o fd1772dma.o
mfmhd_mod-objs	:= mfmhd.o mfm.o

obj-$(CONFIG_BLK_DEV_FD1772)	+= fd1772_mod.o
obj-$(CONFIG_BLK_DEV_MFM)	+= mfmhd.o mfm.o

# Extract lists of the multi-part drivers.
# The 'int-*' lists are intermediate files used to build the multi's.

multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular; remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))
int-m		:= $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y		:= $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

L_OBJS		:= $(filter-out $(export-objs), $(obj-y))
LX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make

fd1772_mod.o: $(FLOPPY)
	$(LD) -r -o $@ $(FLOPPY)

mfmhd_mod.o: mfmhd.o mfm.o
	$(LD) -r -o $@ mfmhd.o mfm.o

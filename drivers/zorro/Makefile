#
# Makefile for the Zorro bus specific drivers.
#

export-objs		:= zorro.o

obj-$(CONFIG_ZORRO)	+= zorro.o names.o
obj-$(CONFIG_PROC_FS)	+= proc.o

host-progs 		:= gen_devlist

include $(TOPDIR)/Rules.make

# Dependencies on generated files need to be listed explicitly

$(obj)/names.o: $(obj)/devlist.h

# And that's how to generate them

$(obj)/devlist.h: $(src)/zorro.ids $(obj)/gen-devlist
	$(obj)/gen-devlist < $<

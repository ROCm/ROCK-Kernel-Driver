#
# Makefile for the Zorro bus specific drivers.
#

obj-$(CONFIG_ZORRO)	+= zorro.o names.o
obj-$(CONFIG_PROC_FS)	+= proc.o

host-progs 		:= gen-devlist

# Files generated that shall be removed upon make clean
clean-files := devlist.h

# Dependencies on generated files need to be listed explicitly

$(obj)/names.o: $(obj)/devlist.h

# And that's how to generate them

$(obj)/devlist.h: $(src)/zorro.ids $(obj)/gen-devlist
	( cd $(obj); ./gen-devlist ) < $<

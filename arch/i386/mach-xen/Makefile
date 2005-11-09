#
# Makefile for the linux kernel.
#

obj-y				+= setup.o
c-obj-y				:= topology.o

$(patsubst %.o,$(obj)/%.c,$(c-obj-y)):
	@ln -fsn $(srctree)/arch/i386/mach-default/$(notdir $@) $@

obj-y	+= $(c-obj-y)

clean-files += $(patsubst %.o,%.c,$(c-obj-y) $(c-obj-))

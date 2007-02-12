obj-y		:= main.o if.o generic.o state.o
obj-$(CONFIG_X86_32) += amd.o cyrix.o centaur.o

ifdef CONFIG_XEN
include $(srctree)/scripts/Makefile.xen
n-obj-xen := generic.o state.o amd.o cyrix.o centaur.o

obj-y := $(call filterxen, $(obj-y), $(n-obj-xen))
obj-y := $(call cherrypickxen, $(obj-y))
endif

#
# Makefile for the dump device drivers.
#

dump-y					:= dump_setup.o dump_fmt.o dump_filters.o dump_scheme.o dump_execute.o
ifeq ($(CONFIG_X86_64),)
ifeq ($(CONFIG_X86),y)
dump-$(CONFIG_X86)			+= dump_i386.o
endif
endif
dump-$(CONFIG_ARM)			+= dump_arm.o
dump-$(CONFIG_PPC64)                    += dump_ppc64.o
dump-$(CONFIG_X86_64)			+= dump_x8664.o
dump-$(CONFIG_CRASH_DUMP_MEMDEV)	+= dump_memdev.o dump_overlay.o
dump-objs				+= $(dump-y)

obj-$(CONFIG_CRASH_DUMP)		+= dump.o
obj-$(CONFIG_CRASH_DUMP_BLOCKDEV)	+= dump_blockdev.o
obj-$(CONFIG_CRASH_DUMP_NETDEV)	+= dump_netdev.o
obj-$(CONFIG_CRASH_DUMP_COMPRESS_RLE)	+= dump_rle.o
obj-$(CONFIG_CRASH_DUMP_COMPRESS_GZIP)	+= dump_gzip.o

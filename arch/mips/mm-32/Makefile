#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

obj-y				+= init.o

obj-$(CONFIG_CPU_TX49XX)	+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_R4300)		+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_R4X00)		+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_VR41XX)	+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_R5000)		+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_NEVADA)	+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_R5432)		+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_RM7000)	+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_R10000)	+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_MIPS32)	+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_MIPS64)	+= pg-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_SB1)		+= tlbex-r4k.o

EXTRA_AFLAGS := $(CFLAGS)

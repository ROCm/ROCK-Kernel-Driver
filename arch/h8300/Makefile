#
# arch/h8300/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# (C) Copyright 2002, Yoshinori Sato <ysato@users.sourceforge.jp>
#
ifndef include-config
-include $(TOPDIR)/.config
endif

platform-$(CONFIG_CPU_H8300H)	:= h8300h
PLATFORM := $(platform-y)

board-$(CONFIG_H8300H_GENERIC)		:= generic
board-$(CONFIG_H8300H_AKI3068NET)	:= ucsimm
board-$(CONFIG_H8300H_H8MAX)		:= ucdimm
board-$(CONFIG_H8300H_SIM)		:= generic
BOARD := $(board-y)

model-$(CONFIG_RAMKERNEL)	:= ram
model-$(CONFIG_ROMKERNEL)	:= rom
MODEL := $(model-y)

cflags-$(CONFIG_CPU_H8300H)		:= -mh
ldflags-$(CONFIG_CPU_H8300H)	:= -mh8300helf

CFLAGS += $(cflags-y)
CFLAGS += -mint32 -fno-builtin
CFLAGS += -O2 -g
CFLAGS += -D__linux__
CFLAGS += -DUTS_SYSNAME=\"uClinux\" -DTARGET=$(BOARD)
AFLAGS += -DPLATFORM=$(PLATFORM) -DTARGET=$(BOARD) -DMODEL=$(MODEL) $(cflags-y)
LDFLAGS += $(ldflags-y)
LDFLAGS_BLOB :=  --format binary --oformat elf32-h8300

CROSS_COMPILE = h8300-elf-
#HEAD := arch/$(ARCH)/platform/$(platform-y)/$(board-y)/crt0_$(model-y).o
LIBGCC := $(shell $(CROSS-COMPILE)$(CC) $(CFLAGS) -print-libgcc-file-name)

core-y	+= arch/$(ARCH)/kernel/ \
	   arch/$(ARCH)/mm/ \
	   arch/$(ARCH)/platform/$(PLATFORM)/ \
	   arch/$(ARCH)/platform/$(PLATFORM)/$(BOARD)/

libs-y	+= arch/$(ARCH)/lib/ $(LIBGCC)

export MODEL

archmrproper:

archclean:
	$(call descend arch/$(ARCH), subdirclean)

prepare: include/asm-$(ARCH)/asm-offsets.h

include/asm-$(ARCH)/asm-offsets.h: arch/$(ARCH)/kernel/asm-offsets.s \
				   include/asm include/linux/version.h
	$(call filechk,gen-asm-offsets)

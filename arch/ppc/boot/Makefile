#
# arch/ppc/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Adapted for PowerPC by Gary Thomas
# modified by Cort (cort@cs.nmt.edu)
#

CFLAGS	+= -fno-builtin -D__BOOTER__ -I$(TOPDIR)/arch/$(ARCH)/boot/include

AFLAGS	+= -D__BOOTER__
OBJCOPY_ARGS = -O elf32-powerpc

subdir-y			:= simple
subdir-$(CONFIG_ALL_PPC)	:= openfirmware prep

HOSTCFLAGS += -Iarch/$(ARCH)/boot/include

tools-$(CONFIG_ALL_PPC)		:= addnote mknote hack-coff mkprep
tools-$(CONFIG_PPLUS)		:= mkbugboot mkprep
tools-$(CONFIG_4xx)		:= mktree
tools-$(CONFIG_LOPEC)		:= mkbugboot mkprep
tools-$(CONFIG_MCPN765)		:= mkbugboot mkprep
tools-$(CONFIG_MENF1)		:= mkprep
tools-$(CONFIG_MVME5100)	:= mkbugboot mkprep
tools-$(CONFIG_PRPMC750)	:= mkbugboot mkprep
tools-$(CONFIG_PRPMC800)	:= mkbugboot mkprep
tools-$(CONFIG_SPRUCE)		:= mktree

all-tools := addnote mknote hack-coff mkprep mkbugboot mktree

host-progs			:= $(addprefix utils/,$(tools-y))

include $(TOPDIR)/Rules.make

zImage zImage.initrd znetboot znetboot.initrd: $(subdir-y)

simple openfirmware prep: lib common images
openfirmware prep: of1275

lib common of1275 images: FORCE
	+@$(call descend,$(obj)/$@,)

openfirmware prep simple: FORCE
	+@$(call descend,$(obj)/$@,$(MAKECMDGOALS))

CLEAN_FILES += $(addprefix $(obj)/utils,$(all-tools))

clean: FORCE
	+@$(call descend,$(obj)/images,clean)

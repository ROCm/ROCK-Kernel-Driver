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

bootdir-y			:= simple
bootdir-$(CONFIG_ALL_PPC)	:= openfirmware prep

# for cleaning...
subdir-				:= images simple openfirmware prep \
				   lib common of1275

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

zImage zImage.initrd znetboot znetboot.initrd: $(addprefix $(obj)/,$(bootdir-y))

.PHONY: FORCE

$(obj)/simple $(obj)/openfirmware $(obj)/prep: $(obj)/lib $(obj)/common $(obj)/images
$(obj)/openfirmware $(obj)/prep: $(obj)/of1275

$(obj)/lib $(obj)/common $(obj)/of1275 $(obj)/images: FORCE
	+@$(call descend,$@,)

$(obj)/openfirmware $(obj)/prep $(obj)/simple: FORCE
	+@$(call descend,$@,$(MAKECMDGOALS))

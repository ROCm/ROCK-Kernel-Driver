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

CFLAGS	 	+= -fno-builtin -D__BOOTER__ -Iarch/$(ARCH)/boot/include
HOSTCFLAGS	+= -Iarch/$(ARCH)/boot/include

BOOT_TARGETS = zImage zImage.initrd znetboot znetboot.initrd

bootdir-y			:= simple
bootdir-$(CONFIG_ALL_PPC)	:= openfirmware prep
subdir-y			:= lib common images
subdir-$(CONFIG_ALL_PPC)	+= of1275

# for cleaning
subdir-				+= simple/ openfirmware/ prep/

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

host-progs			:= $(addprefix utils/,$(tools-y))

.PHONY: $(BOOT_TARGETS) $(bootdir-y)

$(BOOT_TARGETS): $(bootdir-y)

$(bootdir-y): $(addprefix $(obj)/,$(subdir-y)) \
		$(addprefix $(obj)/,$(host-progs))
	$(Q)$(MAKE) $(build)=$(obj)/$@ $(MAKECMDGOALS)

#
# ia64/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1998 by David Mosberger-Tang <davidm@hpl.hp.com>
#

EXTRA_TARGETS := vmlinux.bin vmlinux.gz

targets-$(CONFIG_IA64_HP_SIM)  += bootloader
EXTRA_TARGETS += $(sort $(targets-y))

quiet_cmd_cptotop = LN      $@
      cmd_cptotop = ln -f $< $@

vmlinux.gz: $(obj)/vmlinux.gz $(targets-y)
	$(call cmd,cptotop)
	@echo '  Kernel: $@ is ready'

boot: bootloader

bootloader: $(obj)/bootloader
	$(call cmd,cptotop)

$(obj)/vmlinux.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)


LDFLAGS_bootloader = -static -T

$(obj)/bootloader: $(src)/bootloader.lds $(obj)/bootloader.o \
                   lib/lib.a arch/ia64/lib/lib.a FORCE
	$(call if_changed,ld)

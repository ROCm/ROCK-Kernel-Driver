# $Id: Makefile,v 1.10 2000/02/23 08:17:46 jj Exp $
# Makefile for the Sparc boot stuff.
#
# Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1997,1998 Jakub Jelinek (jj@ultra.linux.cz)

ROOT_IMG	:= /usr/src/root.img
ELFTOAOUT	:= elftoaout

host-progs	:= piggyback btfixupprep
targets		:= tftpboot.img btfix.o btfix.s image

quiet_cmd_elftoaout	= ELFTOAOUT $@
      cmd_elftoaout	= $(ELFTOAOUT) $(obj)/image -o $@
quiet_cmd_piggy		= PIGGY $@
      cmd_piggy		= $(obj)/piggyback $@ $(obj)/System.map $(ROOT_IMG)
quiet_cmd_btfix		= BTFIX $@
      cmd_btfix		= $(OBJDUMP) -x vmlinux | $(obj)/btfixupprep > $@

BTOBJS := $(HEAD_Y) $(INIT_Y)
BTLIBS := $(CORE_Y) $(LIBS_Y) $(DRIVERS_Y) $(NET_Y)
LDFLAGS_image := -T arch/sparc/kernel/vmlinux.lds.s $(BTOBJS) --start-group $(BTLIBS) --end-group $(kallsyms.o)

# Actual linking
$(obj)/image: $(obj)/btfix.o FORCE
	$(call if_changed,ld)
	$(NM) $@ | grep -v  '\(compiled\)\|\(\.o$$\)\|\( [aUw] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)' | sort > $(obj)/System.map

$(obj)/tftpboot.img: $(obj)/piggyback $(obj)/System.map $(obj)/image FORCE
	$(call if_changed,elftoaout)
	$(call if_changed,piggy)

$(obj)/btfix.s: $(obj)/btfixupprep vmlinux FORCE
	$(call if_changed,btfix)

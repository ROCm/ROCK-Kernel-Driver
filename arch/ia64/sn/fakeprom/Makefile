# arch/ia64/sn/fakeprom/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
#  Copyright (c) 2000-2003 Silicon Graphics, Inc.  All rights reserved.
#
# Medusa fake PROM support
#

EXTRA_TARGETS := fpromasm.o main.o fw-emu.o fpmem.o klgraph_init.o \
		 fprom vmlinux.sym

OBJS := $(obj)/fpromasm.o $(obj)/main.o $(obj)/fw-emu.o $(obj)/fpmem.o \
	$(obj)/klgraph_init.o

LDFLAGS_fprom = -static -T

.PHONY: fprom

fprom: $(obj)/fprom

$(obj)/fprom: $(src)/fprom.lds $(OBJS) arch/ia64/lib/lib.a FORCE
	$(call if_changed,ld)

$(obj)/vmlinux.sym: $(src)/make_textsym System.map
	$(src)/make_textsym vmlinux > vmlinux.sym
	$(call cmd,cptotop)

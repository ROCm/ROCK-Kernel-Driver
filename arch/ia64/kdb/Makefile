#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
#

obj-y	 := kdba_bt.o kdba_bp.o kdba_io.o kdbasupport.o \
	    cpu-ia64-opc.o ia64-dis.o ia64-opc.o kdba_id.o kdba_jmp.o

# fru does not compile on 2.6.
# obj-$(CONFIG_IA64_SGI_SN2) += kdba_fru.o
obj-$(CONFIG_IA64_SGI_SN2) += kdba_pod.o

override CFLAGS := $(CFLAGS:%-pg=% )

AFLAGS_kdba_jmp.o += $(AFLAGS_KERNEL)

USE_STANDARD_AS_RULE	:= true

#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (c) 1999-2006 Silicon Graphics, Inc.  All Rights Reserved.
#

obj-$(CONFIG_KDB_MODULES) += kdbm_pg.o kdbm_task.o kdbm_vm.o kdbm_sched.o
ifdef CONFIG_X86
obj-$(CONFIG_KDB_MODULES) += kdbm_x86.o
endif
obj-$(CONFIG_KDB_MODULES_XP) += kdbm_xpc.o
CFLAGS_kdbm_vm.o	+= -I $(srctree)/drivers/scsi

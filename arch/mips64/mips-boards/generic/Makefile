#
# Carsten Langgaard, carstenl@mips.com
# Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
#
# ########################################################################
#
# This program is free software; you can distribute it and/or modify it
# under the terms of the GNU General Public License (Version 2) as
# published by the Free Software Foundation.
# 
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
# 
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
#
# #######################################################################
#
# Makefile for the MIPS boards generic routines under Linux.
#

O_TARGET := mipsboards.o

obj-y                           := mipsIRQ.o pci.o reset.o display.o init.o \
                                   memory.o printf.o cmdline.o time.o
obj-$(CONFIG_REMOTE_DEBUG)      += gdb_hook.o

EXTRA_AFLAGS := $(CFLAGS)

include $(TOPDIR)/Rules.make

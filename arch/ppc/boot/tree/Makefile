# BK Id: SCCS/s.Makefile 1.7 06/15/01 13:16:10 paulus
#
#
#    Module name: Makefile
#
#    Description:
#      Makefile for the IBM "tree" evaluation board Linux kernel
#      boot loaders.
#
#
#    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
# 
#    PPC-405 modification
#    	Copyright 2000-2001 MontaVista Software Inc.
#    	Author: MontaVista Software, Inc.
#          	frank_rowand@mvista.com or source@mvista.com
#  	   	debbie_chu@mvista.com
# 

HOSTCFLAGS = -O -I$(TOPDIR)/include

CC      = $(CROSS_COMPILE)gcc
LD      = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

GZIP	= gzip -vf9
RM	= rm -f
MKEVIMG	= ../utils/mkevimg -l -c
MKIRIMG	= ../utils/mkirimg
CFLAGS	+= -I$(TOPDIR)/drivers/net
LD_ARGS = -e _start -T ld.script -Ttext 0x00200000 -Bstatic

OBJS	= ../common/crt0.o main.o misc.o irSect.o ../common/string.o \
		../common/misc-common.o ../common/ns16550.o
LIBS	= ../lib/zlib.a

treeboot: $(OBJS) $(LIBS) ld.script
	$(LD) -o $@ $(LD_ARGS) $(OBJS) $(LIBS)

zImage:	vmlinux.img

zImage.initrd: vmlinux.initrd.img

treeboot.image: treeboot
	$(OBJCOPY) --add-section=image=../images/vmlinux.gz treeboot $@

treeboot.initrd: treeboot.image ramdisk.image.gz
	$(OBJCOPY) --add-section=initrd=ramdisk.image.gz treeboot.image $@

vmlinux.img: treeboot.image
	$(OBJDUMP) --syms treeboot.image | grep irSectStart > irSectStart.txt
	$(MKIRIMG) treeboot.image treeboot.image.out irSectStart.txt
	$(MKEVIMG) treeboot.image.out ../images/vmlinux.tree.img
	$(RM) treeboot.image treeboot.image.out irSectStart.txt

vmlinux.initrd.img: treeboot.initrd
	$(OBJDUMP) --all-headers treeboot.initrd | grep irSectStart > irSectStart.txt
	$(MKIRIMG) treeboot.initrd treeboot.initrd.out irSectStart.txt
	$(MKEVIMG) treeboot.initrd.out ../images/vmlinux.tree.initrd.img
	$(RM) treeboot.initrd treeboot.initrd.out irSectStart.txt

clean:
	rm -f treeboot treeboot.image treeboot.initrd irSectStart.txt vmlinux.*

include $(TOPDIR)/Rules.make

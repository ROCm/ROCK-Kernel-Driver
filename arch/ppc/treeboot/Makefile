#
#    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
#
#    Module name: Makefile
#
#    Description:
#      Makefile for the IBM "tree" evaluation board Linux kernel
#      boot loaders.
#

HOSTCFLAGS = -O -I$(TOPDIR)/include

GZIP	= gzip -vf9
RM	= rm -f
MKEVIMG	= mkevimg -l
MKIRIMG	= mkirimg

CFLAGS	= -O -fno-builtin -I$(TOPDIR)/include
LD_ARGS = -e _start -T ld.script -Ttext 0x00200000 -Bstatic

OBJS	= crt0.o main.o misc.o irSect.o ../coffboot/string.o ../coffboot/zlib.o 
LIBS	=

treeboot: $(OBJS) ld.script
	$(LD) -o $@ $(LD_ARGS) $(OBJS) $(LIBS)

zImage:	vmlinux.img

zImage.initrd: vmlinux.initrd.img

treeboot.image: treeboot vmlinux.gz
	$(OBJCOPY) --add-section=image=vmlinux.gz treeboot $@

treeboot.initrd: treeboot.image ramdisk.image.gz
	$(OBJCOPY) --add-section=initrd=ramdisk.image.gz treeboot.image $@

vmlinux.img: treeboot.image
	$(OBJDUMP) --syms treeboot.image | grep irSectStart > irSectStart.txt
	$(MKIRIMG) treeboot.image treeboot.image.out irSectStart.txt
	$(MKEVIMG) treeboot.image.out $@
	$(RM) treeboot.image treeboot.image.out irSectStart.txt

vmlinux.initrd.img: treeboot.initrd
	$(OBJDUMP) --all-headers treeboot.initrd | grep irSectStart > irSectStart.txt
	$(MKIRIMG) treeboot.initrd treeboot.initrd.out irSectStart.txt
	$(MKEVIMG) treeboot.initrd.out $@
	$(RM) treeboot.initrd treeboot.initrd.out irSectStart.txt

vmlinux.gz: $(TOPDIR)/vmlinux
	$(OBJCOPY) -S -O binary $(TOPDIR)/vmlinux vmlinux
	$(GZIP) vmlinux

clean:
	rm -f treeboot treeboot.image treeboot.initrd irSectStart.txt vmlinux.* *.o

fastdep:


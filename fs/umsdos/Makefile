#
# Makefile for the umsdos Unix-like filesystem routines.
#

obj-$(CONFIG_UMSDOS) += umsdos.o

umsdos-objs := dir.o  inode.o ioctl.o mangle.o namei.o rdir.o emd.o

p:
	proto *.c >/usr/include/linux/umsdos_fs.p

doc:
	nadoc -i -p umsdos.doc - /tmp/umsdos.mpg

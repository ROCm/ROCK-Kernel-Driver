#
# Makefile for the linux ncp filesystem routines.
#

obj-$(CONFIG_NCP_FS) += ncpfs.o

ncpfs-objs   := dir.o file.o inode.o ioctl.o mmap.o ncplib_kernel.o sock.o \
		ncpsign_kernel.o getopt.o
ifeq ($(CONFIG_NCPFS_EXTRAS),y)
ncpfs-objs   += symlink.o
endif
ifeq ($(CONFIG_NCPFS_NFS_NS),y)
ncpfs-objs   += symlink.o
endif

# If you want debugging output, please uncomment the following line
# EXTRA_CFLAGS += -DDEBUG_NCP=1

CFLAGS_ncplib_kernel.o := -finline-functions

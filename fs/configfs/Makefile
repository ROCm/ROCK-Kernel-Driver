#
# Makefile for the configfs virtual filesystem
#

obj-$(CONFIG_CONFIGFS_FS)      += configfs.o

EXTRA_CFLAGS += -include fs/configfs/kapi-compat/include/kref_init.h

configfs-objs  := inode.o file.o dir.o symlink.o mount.o item.o

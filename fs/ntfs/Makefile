# Rules for making the NTFS driver

O_TARGET := ntfs.o

obj-y   := fs.o sysctl.o support.o util.o inode.o dir.o super.o attr.o
obj-m   := $(O_TARGET)
EXTRA_CFLAGS = -DNTFS_IN_LINUX_KERNEL -DNTFS_VERSION=\"000607\"

include $(TOPDIR)/Rules.make


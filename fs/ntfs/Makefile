# Rules for making the NTFS driver.

O_TARGET := ntfs.o

obj-y   := time.o unistr.o inode.o file.o mft.o super.o debug.o aops.o \
		attrib.o dir.o namei.o mst.o upcase.o compress.o sysctl.o

obj-m   := $(O_TARGET)

EXTRA_CFLAGS = -DNTFS_VERSION=\"2.0.1\"

ifeq ($(CONFIG_NTFS_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

include $(TOPDIR)/Rules.make


# Rules for making the NTFS driver.

O_TARGET := ntfs.o

obj-y   := aops.o attrib.o compress.o debug.o dir.o file.o inode.o mft.o \
		mst.o namei.o super.o sysctl.o time.o unistr.o upcase.o

obj-m   := $(O_TARGET)

EXTRA_CFLAGS = -DNTFS_VERSION=\"2.0.6\"

ifeq ($(CONFIG_NTFS_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

include $(TOPDIR)/Rules.make


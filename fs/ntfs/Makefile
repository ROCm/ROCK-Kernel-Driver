# Rules for making the NTFS TNG driver.

O_TARGET := ntfs.o

obj-y   := time.o unistr.o inode.o file.o mft.o super.o debug.o aops.o \
		attrib.o dir.o namei.o mst.o upcase.o compress.o sysctl.o

obj-m   := $(O_TARGET)

EXTRA_CFLAGS = -DNTFS_VERSION=\"TNG-0.0.8\"

# Uncomment this to enable debugging code.
EXTRA_CFLAGS += -DDEBUG

include $(TOPDIR)/Rules.make


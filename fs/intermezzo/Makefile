#
# Makefile 1.00 Peter Braam <braam@clusterfs.com>
#

obj-$(CONFIG_INTERMEZZO_FS) += intermezzo.o

intermezzo-objs := journal_reiserfs.o cache.o journal.o presto.o vfs.o \
		   psdev.o upcall.o methods.o sysctl.o dcache.o dir.o \
		   super.o journal_ext2.o journal_ext3.o journal_xfs.o \
		   inode.o  file.o journal_obdfs.o

include $(TOPDIR)/Rules.make

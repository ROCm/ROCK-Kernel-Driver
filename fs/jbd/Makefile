#
# Makefile for the linux journaling routines.
#

export-objs := journal.o

obj-$(CONFIG_JBD) += jbd.o

jbd-objs := transaction.o commit.o recovery.o checkpoint.o revoke.o journal.o

include $(TOPDIR)/Rules.make


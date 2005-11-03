#
# Makefile for the Linux kernel device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.

obj-m		+= iscsi_trgt.o
iscsi_trgt-objs	:= tio.o iscsi.o nthread.o wthread.o config.o digest.o \
			conn.o session.o target.o volume.o iotype.o \
			file-io.o null-io.o target_disk.o event.o param.o


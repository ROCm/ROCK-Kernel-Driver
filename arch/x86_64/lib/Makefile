#
# Makefile for x86_64-specific library files.
#

USE_STANDARD_AS_RULE := true

EXTRA_CFLAGS_csum-partial.o := -funroll-loops

L_TARGET = lib.a
obj-y  = csum-partial.o csum-copy.o csum-wrappers.o delay.o \
	usercopy.o getuser.o putuser.o  \
	thunk.o io.o clear_page.o copy_page.o
obj-y += memcpy.o
obj-y += memmove.o
#obj-y += memset.o
obj-y += copy_user.o

export-objs := io.o csum-wrappers.o csum-partial.o

obj-$(CONFIG_IO_DEBUG) += iodebug.o
obj-$(CONFIG_HAVE_DEC_LOCK) += dec_and_lock.o

include $(TOPDIR)/Rules.make

#
# Makefile for x86_64-specific library files.
#

L_TARGET := lib.a
CFLAGS_csum-partial.o := -funroll-loops

obj-y := csum-partial.o csum-copy.o csum-wrappers.o delay.o \
	usercopy.o getuser.o putuser.o  \
	thunk.o io.o clear_page.o copy_page.o bitstr.o
obj-y += memcpy.o memmove.o memset.o copy_user.o

obj-$(CONFIG_IO_DEBUG) += iodebug.o
obj-$(CONFIG_HAVE_DEC_LOCK) += dec_and_lock.o

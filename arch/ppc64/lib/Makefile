#
# Makefile for ppc64-specific library files..
#

lib-y := checksum.o dec_and_lock.o string.o strcase.o
lib-y += copypage.o memcpy.o copyuser.o

# Lock primitives are defined as no-ops in include/linux/spinlock.h
# for non-SMP configs. Don't build the real versions.

lib-$(CONFIG_SMP) += locks.o

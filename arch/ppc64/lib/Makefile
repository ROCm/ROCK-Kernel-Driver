#
# Makefile for ppc64-specific library files..
#

lib-y := checksum.o dec_and_lock.o string.o strcase.o
lib-y += copypage.o memcpy.o copyuser.o

lib-$(CONFIG_PPC_ISERIES) += locks.o
lib-$(CONFIG_PPC_SPLPAR)  += locks.o

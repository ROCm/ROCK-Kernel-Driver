#
# Makefile for the kernel security code
#

# Objects that export symbols
export-objs	:= security.o

# Object file lists
obj-y		:= security.o dummy.o

obj-$(CONFIG_SECURITY_CAPABILITIES)	+= capability.o

include $(TOPDIR)/Rules.make

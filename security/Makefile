#
# Makefile for the kernel security code
#

# Objects that export symbols
export-objs	:= security.o capability.o

# if we don't select a security model, use the default capabilities
ifneq ($(CONFIG_SECURITY),y)
obj-y		+= capability.o
endif

# Object file lists
obj-$(CONFIG_SECURITY)			+= security.o dummy.o
obj-$(CONFIG_SECURITY_CAPABILITIES)	+= capability.o

include $(TOPDIR)/Rules.make

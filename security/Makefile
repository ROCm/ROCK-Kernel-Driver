#
# Makefile for the kernel security code
#

# if we don't select a security model, use the default capabilities
ifneq ($(CONFIG_SECURITY),y)
obj-y		+= capability.o
endif

# Object file lists
obj-$(CONFIG_SECURITY)			+= security.o dummy.o
obj-$(CONFIG_SECURITY_CAPABILITIES)	+= capability.o
obj-$(CONFIG_SECURITY_ROOTPLUG)		+= root_plug.o

#
# Makefile for the kernel security code
#

subdir-$(CONFIG_SECURITY_SELINUX)	+= selinux

# if we don't select a security model, use the default capabilities
ifneq ($(CONFIG_SECURITY),y)
obj-y		+= commoncap.o capability.o
endif

# Object file lists
obj-$(CONFIG_SECURITY)			+= security.o dummy.o
# Must precede capability.o in order to stack properly.
ifeq ($(CONFIG_SECURITY_SELINUX),y)
	obj-$(CONFIG_SECURITY_SELINUX)	+= selinux/built-in.o
endif
obj-$(CONFIG_SECURITY_CAPABILITIES)	+= commoncap.o capability.o
obj-$(CONFIG_SECURITY_ROOTPLUG)		+= commoncap.o root_plug.o

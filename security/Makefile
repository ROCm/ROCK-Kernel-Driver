#
# Makefile for the kernel security code
#

subdir-$(CONFIG_SECURITY_SELINUX)	+= selinux

# We always need commoncap as it's default
obj-y		+= commoncap.o

# Object file lists
obj-$(CONFIG_SECURITY)			+= security.o dummy.o
# Must precede capability.o in order to stack properly.
obj-$(CONFIG_SECURITY_SELINUX)		+= selinux/built-in.o
obj-$(CONFIG_SECURITY_ROOTPLUG)		+= root_plug.o
obj-$(CONFIG_SECURITY_CAPABILITIES)	+= capability.o

#
# Makefile for the kernel security code
#

obj-$(CONFIG_KEYS)			+= keys/
subdir-$(CONFIG_SECURITY_SELINUX)	+= selinux

# We always need commoncap as it's default
obj-y		+= commoncap.o

# Object file lists
obj-$(CONFIG_SECURITY)			+= security.o dummy.o inode.o
# Must precede capability.o in order to stack properly.
obj-$(CONFIG_SECURITY_SELINUX)		+= selinux/built-in.o
obj-$(CONFIG_SECURITY_APPARMOR)		+= commoncap.o apparmor/
obj-$(CONFIG_SECURITY_CAPABILITIES)	+= capability.o
obj-$(CONFIG_SECURITY_ROOTPLUG)		+= root_plug.o

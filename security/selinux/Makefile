#
# Makefile for building the SELinux module as part of the kernel tree.
#

obj-$(CONFIG_SECURITY_SELINUX) := selinux.o ss/

selinux-objs := avc.o hooks.o selinuxfs.o

ifeq ($(CONFIG_SECURITY_NETWORK),y)
	selinux-objs += netif.o
endif

EXTRA_CFLAGS += -Isecurity/selinux/include


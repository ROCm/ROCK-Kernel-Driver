#
# Makefile for building the SELinux module as part of the kernel tree.
#

obj-$(CONFIG_SECURITY_SELINUX) := selinux.o ss/

selinux-objs := avc.o hooks.o selinuxfs.o

EXTRA_CFLAGS += -Isecurity/selinux/include


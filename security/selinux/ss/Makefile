#
# Makefile for building the SELinux security server as part of the kernel tree.
#

EXTRA_CFLAGS += -Isecurity/selinux/include -include security/selinux/ss/global.h

obj-y := ss.o

ss-objs := ebitmap.o hashtab.o symtab.o sidtab.o avtab.o policydb.o services.o

ifeq ($(CONFIG_SECURITY_SELINUX_MLS),y)
ss-objs += mls.o
endif


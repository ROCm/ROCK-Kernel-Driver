#
# Makefile for Linux kernel SUN RPC
#

obj-$(CONFIG_SUNRPC) += sunrpc.o

export-objs := sunrpc_syms.o

sunrpc-y := clnt.o xprt.o sched.o \
	    auth.o auth_null.o auth_unix.o \
	    svc.o svcsock.o svcauth.o \
	    pmap_clnt.o xdr.o sunrpc_syms.o
sunrpc-$(CONFIG_PROC_FS) += stats.o
sunrpc-$(CONFIG_SYSCTL) += sysctl.o
sunrpc-objs := $(sunrpc-y)

include $(TOPDIR)/Rules.make

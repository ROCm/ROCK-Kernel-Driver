#
# Makefile for Linux kernel Rx RPC
#

export-objs := rxrpc_syms.o

rxrpc-objs := \
	call.o \
	connection.o \
	krxiod.o \
	krxsecd.o \
	krxtimod.o \
	main.o \
	peer.o \
	rxrpc_syms.o \
	transport.o

#ifeq ($(CONFIG_PROC_FS),y)
rxrpc-objs	+= proc.o
#endif
#ifeq ($(CONFIG_SYSCTL),y)
rxrpc-objs += sysctl.o
#endif

obj-m  := rxrpc.o

# superfluous for 2.5, but needed for 2.4..
ifeq "$(VERSION).$(PATCHLEVEL)" "2.4"
rxrpc.o: $(rxrpc-objs)
	$(LD) -r -o $@ $(rxrpc-objs)
endif

include $(TOPDIR)/Rules.make

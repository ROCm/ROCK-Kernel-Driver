#
# Makefile for Linux kernel Rx RPC
#

rxrpc-y := \
	call.o \
	connection.o \
	krxiod.o \
	krxsecd.o \
	krxtimod.o \
	main.o \
	peer.o \
	rxrpc_syms.o \
	transport.o

rxrpc-$(CONFIG_PROC_FS) += proc.o
rxrpc-$(CONFIG_SYSCTL)  += sysctl.o

obj-$(CONFIG_RXRPC) := rxrpc.o

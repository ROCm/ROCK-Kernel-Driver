#
# Makefile for Linux kernel rpcsec_gss implementation
#

obj-$(CONFIG_SUNRPC_GSS) += auth_rpcgss.o

export-objs := sunrpcgss_syms.o

auth_rpcgss-objs := auth_gss.o gss_pseudoflavors.o gss_generic_token.o \
	sunrpcgss_syms.o gss_mech_switch.o


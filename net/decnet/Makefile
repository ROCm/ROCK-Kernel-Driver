
obj-$(CONFIG_DECNET) += decnet.o

decnet-y := af_decnet.o dn_nsp_in.o dn_nsp_out.o dn_route.o dn_dev.o dn_neigh.o dn_timer.o
decnet-$(CONFIG_DECNET_ROUTER) += dn_fib.o dn_rules.o dn_table.o
decnet-$(CONFIG_DECNET_FW) += dn_fw.o
decnet-y += sysctl_net_decnet.o
decnet-objs := $(decnet-y)

include $(TOPDIR)/Rules.make


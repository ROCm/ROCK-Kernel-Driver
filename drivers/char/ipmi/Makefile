#
# Makefile for the ipmi drivers.
#

ipmi_kcs_drv-objs := ipmi_kcs_sm.o ipmi_kcs_intf.o

obj-$(CONFIG_IPMI_HANDLER) += ipmi_msghandler.o
obj-$(CONFIG_IPMI_DEVICE_INTERFACE) += ipmi_devintf.o
obj-$(CONFIG_IPMI_KCS) += ipmi_kcs_drv.o
obj-$(CONFIG_IPMI_WATCHDOG) += ipmi_watchdog.o

ipmi_kcs_drv.o:	$(ipmi_kcs_drv-objs)
	$(LD) -r -o $@ $(ipmi_kcs_drv-objs) 

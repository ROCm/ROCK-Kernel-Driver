#
# USB peripheral controller drivers
#
obj-$(CONFIG_USB_NET2280)	+= net2280.o

#
# USB gadget drivers
#
g_zero-objs			:= zero.o usbstring.o
g_ether-objs			:= ether.o usbstring.o
gadgetfs-objs			:= inode.o usbstring.o
 
obj-$(CONFIG_USB_ZERO)		+= g_zero.o
obj-$(CONFIG_USB_ETH)		+= g_ether.o
obj-$(CONFIG_USB_GADGETFS)	+= gadgetfs.o


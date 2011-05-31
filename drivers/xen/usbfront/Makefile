obj-$(CONFIG_XEN_USB_FRONTEND) := xen-hcd.o

xen-hcd-y   := usbfront-hcd.o xenbus.o

ifeq ($(CONFIG_XEN_USB_FRONTEND_HCD_STATS),y)
EXTRA_CFLAGS += -DXENHCD_STATS
endif

ifeq ($(CONFIG_XEN_USB_FRONTEND_HCD_PM),y)
EXTRA_CFLAGS += -DXENHCD_PM
endif

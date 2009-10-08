obj-$(CONFIG_PARAVIRT_XEN)	+= grant-table.o features.o events.o manage.o
xen-hotplug-$(CONFIG_PARAVIRT_XEN) := cpu_hotplug.o
xen-xencomm-$(CONFIG_PARAVIRT_XEN) := xencomm.o
xen-balloon-$(CONFIG_PARAVIRT_XEN) := balloon.o

xen-balloon-$(CONFIG_XEN)	:= balloon/
obj-$(CONFIG_XEN)		+= core/
obj-$(CONFIG_XEN)		+= console/
obj-$(CONFIG_XEN)		+= evtchn/
obj-y				+= xenbus/
obj-$(CONFIG_XEN)		+= char/

nostackp := $(call cc-option, -fno-stack-protector)
ifeq ($(CONFIG_PARAVIRT_XEN),y)
CFLAGS_features.o			:= $(nostackp)
endif

obj-$(CONFIG_XEN)		+= features.o util.o
obj-$(CONFIG_HOTPLUG_CPU)	+= $(xen-hotplug-y)
obj-$(CONFIG_XEN_XENCOMM)	+= $(xen-xencomm-y)
obj-$(CONFIG_XEN_BALLOON)	+= $(xen-balloon-y)
obj-$(CONFIG_XEN_DEV_EVTCHN)	+= evtchn.o
obj-$(CONFIG_XENFS)		+= xenfs/
obj-$(CONFIG_XEN_SYS_HYPERVISOR)	+= sys-hypervisor.o
obj-$(CONFIG_XEN_BLKDEV_BACKEND)	+= blkback/
obj-$(CONFIG_XEN_BLKDEV_TAP)		+= blktap/
obj-$(CONFIG_XEN_BLKDEV_TAP2)           += blktap2/
obj-$(CONFIG_XEN_NETDEV_BACKEND)	+= netback/
obj-$(CONFIG_XEN_TPMDEV_BACKEND)	+= tpmback/
obj-$(CONFIG_XEN_BLKDEV_FRONTEND)	+= blkfront/
obj-$(CONFIG_XEN_NETDEV_FRONTEND)	+= netfront/
obj-$(CONFIG_XEN_PCIDEV_BACKEND)	+= pciback/
obj-$(CONFIG_XEN_PCIDEV_FRONTEND)	+= pcifront/
obj-$(CONFIG_XEN_FRAMEBUFFER)		+= fbfront/
obj-$(CONFIG_XEN_KEYBOARD)		+= fbfront/
obj-$(CONFIG_XEN_SCSI_BACKEND)		+= scsiback/
obj-$(CONFIG_XEN_SCSI_FRONTEND)		+= scsifront/
obj-$(CONFIG_XEN_USB_BACKEND)		+= usbback/
obj-$(CONFIG_XEN_USB_FRONTEND)		+= usbfront/
obj-$(CONFIG_XEN_PRIVCMD)	+= privcmd/
obj-$(CONFIG_XEN_GRANT_DEV)	+= gntdev/
obj-$(CONFIG_XEN_NETDEV_ACCEL_SFC_UTIL)		+= sfc_netutil/
obj-$(CONFIG_XEN_NETDEV_ACCEL_SFC_FRONTEND)	+= sfc_netfront/
obj-$(CONFIG_XEN_NETDEV_ACCEL_SFC_BACKEND)	+= sfc_netback/

# Makefile for the hysdn ISDN device driver

# Each configuration option enables a list of files.

obj-$(CONFIG_HYSDN)	+= hysdn.o

# Multipart objects.

hysdn-objs	:= hysdn_procconf.o hysdn_proclog.o boardergo.o hysdn_boot.o \
		   hysdn_sched.o hysdn_net.o hysdn_init.o

# Optional parts of multipart objects.

hysdn-objs-$(CONFIG_HYSDN_CAPI) += hycapi.o

hysdn-objs 	+= $(hysdn-objs-y)

include $(TOPDIR)/Rules.make

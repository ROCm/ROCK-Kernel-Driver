# Makefile for the Linux device tree

obj-y			:= core.o sys.o interface.o power.o bus.o \
			   driver.o class.o intf.o platform.o \
			   cpu.o firmware.o init.o
obj-$(CONFIG_NUMA)	+= node.o  memblk.o
obj-y			+= fs/
obj-$(CONFIG_HOTPLUG)	+= hotplug.o

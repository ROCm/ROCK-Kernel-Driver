#
# Makefile for the S/390 common i/o drivers
#

obj-y += airq.o blacklist.o chsc.o cio.o css.o requestirq.o 
export-objs += airq.o css.o chsc.o cio.o requestirq.o

ccw_device-objs += device.o device_fsm.o device_ops.o
ccw_device-objs += device_id.o device_pgid.o device_status.o
obj-y += ccw_device.o
export-objs += device.o device_ops.o 

obj-$(CONFIG_CCWGROUP) += ccwgroup.o
export-objs += ccwgroup.o

obj-$(CONFIG_QDIO) += qdio.o
export-objs += qdio.o

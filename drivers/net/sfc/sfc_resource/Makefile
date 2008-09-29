obj-$(CONFIG_SFC_RESOURCE) := sfc_resource.o

EXTRA_CFLAGS += -D__CI_HARDWARE_CONFIG_FALCON__
EXTRA_CFLAGS += -D__ci_driver__
EXTRA_CFLAGS += -Werror
EXTRA_CFLAGS += -Idrivers/net/sfc -Idrivers/net/sfc/sfc_resource

sfc_resource-objs := resource_driver.o iopage.o efx_vi_shm.o \
	driverlink_new.o kernel_proc.o kfifo.o \
	nic.o eventq.o falcon.o falcon_hash.o \
	assert_valid.o buddy.o buffer_table.o filter_resource.o \
	iobufset_resource.o resource_manager.o resources.o \
	vi_resource_alloc.o vi_resource_event.o vi_resource_flush.o \
	vi_resource_manager.o driver_object.o kernel_compat.o

EXTRA_CFLAGS += -Idrivers/infiniband/include

obj-$(CONFIG_INFINIBAND) +=	ib_core.o

ib_core-y :=			packer.o ud_header.o verbs.o sysfs.o \
				device.o fmr_pool.o cache.o

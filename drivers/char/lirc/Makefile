#
# Makefile for the lirc drivers
#

obj-$(CONFIG_LIRC_SUPPORT) += lirc_dev.o lirc_atiusb.o lirc_bt829.o \
	lirc_igorplugusb.o lirc_it87.o lirc_mceusb.o lirc_sir.o

obj-$(CONFIG_LIRC_I2C)		+= lirc_i2c.o
obj-$(CONFIG_LIRC_GPIO)		+= lirc_gpio.o
obj-$(CONFIG_LIRC_PARALLEL)	+= lirc_parallel.o
obj-$(CONFIG_LIRC_SERIAL)	+= lirc_serial.o
obj-$(CONFIG_LIRC_BROKEN)	+= lirc_sasem.o	# doesn't build on 2.6

EXTRA_CFLAGS += -DKERNEL26=1
EXTRA_CFLAGS += -DIRCTL_DEV_MAJOR=60
EXTRA_CFLAGS += -DDEV_LIRC='"lirc"'
EXTRA_CFLAGS += -DLIRC_PORT=0x3f8
EXTRA_CFLAGS += -DLIRC_IRQ=4
EXTRA_CFLAGS += -DLIRC_TIMER=1

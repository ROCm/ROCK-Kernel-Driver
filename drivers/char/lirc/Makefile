#
# Makefile for the lirc drivers
#

obj-$(CONFIG_LIRC_SUPPORT) += lirc_dev.o
obj-$(CONFIG_LIRC_GPIO) += lirc_gpio.o
obj-$(CONFIG_LIRC_BT829) += lirc_bt829.o
obj-$(CONFIG_LIRC_IT87) += lirc_it87.o
obj-$(CONFIG_LIRC_PARALLEL) += lirc_parallel.o
obj-$(CONFIG_LIRC_SERIAL) += lirc_serial.o
obj-$(CONFIG_LIRC_SIR) += lirc_sir.o
obj-$(CONFIG_LIRC_ATIUSB) += lirc_atiusb.o
obj-$(CONFIG_LIRC_MCEUSB) += lirc_mceusb.o
obj-$(CONFIG_LIRC_I2C) += lirc_i2c.o

EXTRA_CFLAGS := -DLIRC26=1

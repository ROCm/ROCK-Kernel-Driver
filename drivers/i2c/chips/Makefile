#
# Makefile for the kernel hardware sensors chip drivers.
#

# asb100, then w83781d go first, as they can override other drivers' addresses.
obj-$(CONFIG_SENSORS_ASB100)	+= asb100.o
obj-$(CONFIG_SENSORS_W83627HF)	+= w83627hf.o
obj-$(CONFIG_SENSORS_W83781D)	+= w83781d.o

obj-$(CONFIG_SENSORS_ADM1021)	+= adm1021.o
obj-$(CONFIG_SENSORS_DS1621)	+= ds1621.o
obj-$(CONFIG_SENSORS_EEPROM)	+= eeprom.o
obj-$(CONFIG_SENSORS_FSCHER)	+= fscher.o
obj-$(CONFIG_SENSORS_GL518SM)	+= gl518sm.o
obj-$(CONFIG_SENSORS_IT87)	+= it87.o
obj-$(CONFIG_SENSORS_LM75)	+= lm75.o
obj-$(CONFIG_SENSORS_LM78)	+= lm78.o
obj-$(CONFIG_SENSORS_LM80)	+= lm80.o
obj-$(CONFIG_SENSORS_LM83)	+= lm83.o
obj-$(CONFIG_SENSORS_LM85)	+= lm85.o
obj-$(CONFIG_SENSORS_LM90)	+= lm90.o
obj-$(CONFIG_SENSORS_PCF8574)	+= pcf8574.o
obj-$(CONFIG_SENSORS_PCF8591)	+= pcf8591.o
obj-$(CONFIG_SENSORS_VIA686A)	+= via686a.o
obj-$(CONFIG_SENSORS_W83L785TS)	+= w83l785ts.o

ifeq ($(CONFIG_I2C_DEBUG_CHIP),y)
EXTRA_CFLAGS += -DDEBUG
endif

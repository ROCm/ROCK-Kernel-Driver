# Makefile for the CAPI subsystem.

# The target object and module list name.

O_TARGET	:= built-in.o

# Objects that export symbols.

export-objs	:= kcapi.o capiutil.o capilib.o capifs.o

# Multipart objects.

kernelcapi-objs	:= kcapi.o capiutil.o capilib.o

# Ordering constraints: kernelcapi.o first

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_CAPI)			+= kernelcapi.o
obj-$(CONFIG_ISDN_CAPI_CAPI20)		+= capi.o 
obj-$(CONFIG_ISDN_CAPI_CAPIDRV)		+= capidrv.o
obj-$(CONFIG_ISDN_CAPI_CAPIFS)		+= capifs.o

# The global Rules.make.

include $(TOPDIR)/Rules.make

#
# Makefile for arch/m68knommu/platform/68VZ328.
#

obj-y := $(BOARD)/config.o

EXTRA_TARGETS := $(BOARD)/bootlogo.rh $(BOARD)/crt0_$(MODEL).o

$(obj)/$(BOARD)/bootlogo.rh: $(src)/../68EZ328/bootlogo.h
	perl $(src)/../68328/bootlogo.pl < $(src)/../68EZ328/bootlogo.h \
		> $(obj)/$(BOARD)/bootlogo.rh


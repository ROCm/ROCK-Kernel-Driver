#
# Makefile for AMD library routines, which are used by AMD driver
# components.
#
# This is for common library routines that can be shared between AMD
# driver components or later moved to kernel/lib for sharing with
# other drivers.

ifneq (,$(BUILD_AS_DKMS))
	CHASH_NAME = amdchash
	$(CHASH_NAME)-y := chash.o
else
	CHASH_NAME = chash
endif

ccflags-y := -I$(src)/../include

obj-$(CONFIG_CHASH) += $(CHASH_NAME).o

LIB_FULL_PATH = $(src)
include $(LIB_FULL_PATH)/backport/Makefile

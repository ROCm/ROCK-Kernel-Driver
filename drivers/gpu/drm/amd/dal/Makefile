#
# Makefile for the DAL (Display Abstract Layer), which is a  sub-component
# of AMDSOC/AMDGPU drm driver.
# It provides the HW control for display related functionalities.

# path relative to AMDSOC/AMDGPU
ifneq ($(CONFIG_DRM_AMDSOC),)
AMDDALPATH = ../amd/dal
endif

ifneq ($(CONFIG_DRM_AMDGPU),)
AMDDALPATH = ../dal
endif

subdir-ccflags-y += -I$(AMDDALPATH)/ -I$(AMDDALPATH)/include -DDAL_CZ_BRINGUP

DAL_LIBS = adapter audio asic_capability basics bios connector controller dcs \
	display_path display_service encoder gpio gpu hw_sequencer i2caux \
	interface link_service mode_manager timing_service topology irq

AMD_DAL = $(addsuffix /Makefile, $(addprefix drivers/gpu/drm/amd/dal/,$(DAL_LIBS)))

include $(AMD_DAL)

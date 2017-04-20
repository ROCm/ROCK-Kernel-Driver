#
# Makefile for the 'dm' sub-component of DAL.
# It provides the control and status of dm blocks.



KCLDM = kcl_dm_types.o kcl_dm.o kcl_dm_irq.o kcl_dm_mst_types.o

ifneq ($(CONFIG_DRM_AMD_DC),)
KCLDM += kcl_dm_services.o kcl_dm_helpers.o
endif

subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/dc

KCL_DM = $(addprefix $(AMDDALPATH)/kcl_dm/,$(KCLDM))

AMD_DISPLAY_FILES += $(KCL_DM)

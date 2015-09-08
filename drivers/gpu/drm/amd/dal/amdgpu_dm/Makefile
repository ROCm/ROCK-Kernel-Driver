#
# Makefile for the 'dm' sub-component of DAL.
# It provides the control and status of dm blocks.



AMDGPUDM = amdgpu_dal_services.o amdgpu_dm_types.o amdgpu_dm.o amdgpu_dm_irq.o

AMDGPU_DM = $(addprefix $(AMDDALPATH)/amdgpu_dm/,$(AMDGPUDM))

AMD_DAL_FILES += $(AMDGPU_DM)

#
# Makefile for the DAL (Display Abstract Layer), which is a  sub-component
# of the AMDGPU drm driver.
# It provides the HW control for display related functionalities.

AMDDALPATH = $(RELATIVE_AMD_DAL_PATH)

subdir-ccflags-y += -Werror

subdir-ccflags-y += -I$(AMDDALPATH)/ -I$(AMDDALPATH)/include

subdir-ccflags-y += -I$(FULL_AMD_DAL_PATH)/dc/inc/
subdir-ccflags-y += -I$(FULL_AMD_DAL_PATH)/dc/inc/hw

#TODO: remove when Timing Sync feature is complete
subdir-ccflags-y += -DBUILD_FEATURE_TIMING_SYNC=0

DAL_LIBS = amdgpu_dm dc

AMD_DAL = $(addsuffix /Makefile, $(addprefix $(FULL_AMD_DAL_PATH)/,$(DAL_LIBS)))

include $(AMD_DAL)

#
# Makefile for the 'hw_sequencer' sub-component of DAL.
# It provides hw programming sequences implementation.

HWS = hw_sequencer.o hw_path_mode_set.o hw_sequencer_helpers.o \
	hw_sync_control.o hw_sequencer_adjustments.o hw_sequencer_parameters.o

AMD_DAL_HWS = $(addprefix $(AMDDALPATH)/hw_sequencer/,$(HWS))

AMD_DAL_FILES += $(AMD_DAL_HWS)

###############################################################################
# DCE 11x family
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
HWS_DCE110 = hw_sequencer_dce110.o hw_sync_control_dce110.o

AMD_DAL_HWS_DCE110 = $(addprefix \
	$(AMDDALPATH)/hw_sequencer/dce110/,$(HWS_DCE110))

AMD_DAL_FILES += $(AMD_DAL_HWS_DCE110)
endif

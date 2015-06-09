#
# Makefile for the 'encoder' sub-component of DAL.
# It provides the control and status for encoder block,
# which includes PHY and DIG HW blocks.
#
# Please use the convention of putting the files in the same line
# (up to the line MAX limit) and don't include the commented-out files.

ENCODER = encoder_impl.o encoder.o hw_ctx_analog_encoder.o \
		analog_encoder.o analog_encoder_crt.o hw_ctx_digital_encoder.o \
		hw_ctx_digital_encoder_hal.o \
		hw_ctx_external_digital_encoder_hal.o \
		digital_encoder.o digital_encoder_dp.o \
		external_digital_encoder.o \
		wireless_encoder.o

AMD_DAL_ENCODER = $(addprefix $(AMDDALPATH)/encoder/,$(ENCODER))

AMD_DAL_FILES += $(AMD_DAL_ENCODER)

###############################################################################
# DCE 11x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
ENCODER_DCE11 = digital_encoder_dp_dce110.o digital_encoder_edp_dce110.o \
	digital_encoder_uniphy_dce110.o \
	hw_ctx_digital_encoder_dce110.o

AMD_DAL_ENCODER_DCE11 = $(addprefix $(AMDDALPATH)/encoder/dce110/,$(ENCODER_DCE11))

AMD_DAL_FILES += $(AMD_DAL_ENCODER_DCE11)
endif

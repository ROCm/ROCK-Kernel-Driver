#
# Makefile for the 'audio' sub-component of DAL.
# It provides the control and status of HW adapter resources,
# that are global for the ASIC and sharable between pipes.

AUDIO = audio_base.o hw_ctx_audio.o

AMD_DAL_AUDIO = $(addprefix $(AMDDALPATH)/dc/audio/,$(AUDIO))

AMD_DAL_FILES += $(AMD_DAL_AUDIO)


###############################################################################
# DCE 11x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
AUDIO_DCE11 = audio_dce110.o hw_ctx_audio_dce110.o

AMD_DAL_AUDIO_DCE11 = $(addprefix $(AMDDALPATH)/dc/audio/dce110/,$(AUDIO_DCE11))

AMD_DAL_FILES += $(AMD_DAL_AUDIO_DCE11)
endif

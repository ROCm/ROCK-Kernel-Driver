#
# Makefile for the 'gpu' sub-component of DAL.
# It provides the control and status of HW adapter resources,
# that are global for the ASIC and sharable between pipes.

GPU = dc_clock_generator.o display_clock.o divider_range.o

AMD_DAL_GPU = $(addprefix $(AMDDALPATH)/dc/gpu/,$(GPU))

AMD_DAL_FILES += $(AMD_DAL_GPU)

###############################################################################
# DCE 80 family
###############################################################################

ifdef CONFIG_DRM_AMD_DAL_DCE8_0
GPU_DCE80 = display_clock_dce80.o dc_clock_gating_dce80.o

AMD_DAL_GPU_DCE80 = $(addprefix $(AMDDALPATH)/dc/gpu/dce80/,$(GPU_DCE80))

AMD_DAL_FILES += $(AMD_DAL_GPU_DCE80)
endif


###############################################################################
# DCE 110 family
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
GPU_DCE110 = display_clock_dce110.o dc_clock_gating_dce110.o

AMD_DAL_GPU_DCE110 = $(addprefix $(AMDDALPATH)/dc/gpu/dce110/,$(GPU_DCE110))

AMD_DAL_FILES += $(AMD_DAL_GPU_DCE110)
endif

ifdef CONFIG_DRM_AMD_DAL_DCE11_2
GPU_DCE112 = display_clock_dce112.o dc_clock_gating_dce112.o

AMD_DAL_GPU_DCE112 = $(addprefix $(AMDDALPATH)/dc/gpu/dce112/,$(GPU_DCE112))

AMD_DAL_FILES += $(AMD_DAL_GPU_DCE110) $(AMD_DAL_GPU_DCE112)
endif

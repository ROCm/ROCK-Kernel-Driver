#
# Makefile for the 'gpu' sub-component of DAL.
# It provides the control and status of HW adapter resources,
# that are global for the ASIC and sharable between pipes.

GPU = gpu.o bandwidth_manager.o display_clock.o dc_clock_generator.o \
	divider_range.o bandwidth_manager.o clock_source.o \
	pll_clock_source.o ext_clock_source.o calc_pll_clock_source.o

AMD_DAL_GPU = $(addprefix $(AMDDALPATH)/gpu/,$(GPU))

AMD_DAL_FILES += $(AMD_DAL_GPU)

###############################################################################
# DCE 110 family
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
GPU_DCE110 = bandwidth_manager_dce110.o display_clock_dce110.o \
	gpu_dce110.o pll_clock_source_dce110.o ext_clock_source_dce110.o \
	vce_clock_source_dce110.o dc_clock_gating_dce110.o

AMD_DAL_GPU_DCE110 = $(addprefix $(AMDDALPATH)/gpu/dce110/,$(GPU_DCE110))

AMD_DAL_FILES += $(AMD_DAL_GPU_DCE110)
endif

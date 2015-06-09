#
# Makefile for the 'controller' sub-component of DAL.
# It provides the control and status of HW CRTC block.

CONTROLLER = controller.o timing_generator.o csc_video.o \
		csc_grph.o scaler.o grph_gamma.o video_gamma.o \
		scaler_filter.o graphics_and_video_gamma.o fbc.o formatter.o \
		csc.o surface.o line_buffer.o cursor.o

AMD_DAL_CONTROLLER = $(addprefix $(AMDDALPATH)/controller/,$(CONTROLLER))

AMD_DAL_FILES += $(AMD_DAL_CONTROLLER)

###############################################################################
# DCE 11x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
CONTROLLER_DCE110 = controller_dce110.o fbc_dce110.o vga_dce110.o \
		timing_generator_dce110.o dcp_bit_depth_reduction_dce110.o \
		csc_dce110.o csc_grph_dce110.o formatter_dce110.o \
		grph_gamma_dce110.o scaler_dce110.o pipe_control_dce110.o \
		surface_dce110.o controller_v_dce110.o scaler_v_dce110.o \
		surface_v_dce110.o timing_generator_v_dce110.o \
		pipe_control_v_dce110.o line_buffer_dce110.o \
		line_buffer_v_dce110.o col_man_csc_dce110.o \
		col_man_gamma_dce110.o cursor_dce110.o

AMD_DAL_CONTROLLER_DCE110 = $(addprefix \
	$(AMDDALPATH)/controller/dce110/,$(CONTROLLER_DCE110))

AMD_DAL_FILES += $(AMD_DAL_CONTROLLER_DCE110)
endif

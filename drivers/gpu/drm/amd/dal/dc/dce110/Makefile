#
# Makefile for the 'controller' sub-component of DAL.
# It provides the control and status of HW CRTC block.

DCE110 = dce110_ipp.o dce110_ipp_cursor.o \
dce110_ipp_gamma.o dce110_link_encoder.o dce110_opp.o \
dce110_opp_formatter.o dce110_opp_regamma.o dce110_stream_encoder.o \
dce110_timing_generator.o dce110_transform.o dce110_transform_v.o \
dce110_transform_gamut.o dce110_transform_scl.o dce110_opp_csc.o\
dce110_compressor.o dce110_mem_input.o dce110_hw_sequencer.o \
dce110_resource.o dce110_transform_bit_depth.o dce110_clock_source.o \
dce110_opp_regamma_v.o dce110_opp_csc_v.o dce110_timing_generator_v.o \
dce110_mem_input_v.o dce110_opp_v.o

AMD_DAL_DCE110 = $(addprefix $(AMDDALPATH)/dc/dce110/,$(DCE110))

AMD_DAL_FILES += $(AMD_DAL_DCE110)

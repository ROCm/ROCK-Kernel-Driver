#
# Makefile for the 'timing_service' sub-component of DAL.
# It the functionality of timings management.

MODE_TIMING_SOURCE = mode_timing_source.o mode_timing_source_cea770.o \
	mode_timing_source_cea861d.o mode_timing_source_cea861e.o \
	mode_timing_source_cvt.o mode_timing_source_dmt.o \
	mode_timing_source_gtf.o mode_timing_source_hdmi_vic.o \

TIMING_SERVICE = $(MODE_TIMING_SOURCE) timing_service.o mode_timing_list.o \
	mode_timing_filter.o default_mode_list.o timing_service_types.o

AMD_DAL_TIMING_SERVICE = $(addprefix $(AMDDALPATH)/timing_service/,$(TIMING_SERVICE))

AMD_DAL_FILES += $(AMD_DAL_TIMING_SERVICE)

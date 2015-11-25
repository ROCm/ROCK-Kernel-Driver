#
# Makefile for the 'gpu' sub-component of DAL.
# It provides the control and status of HW adapter resources,
# that are global for the ASIC and sharable between pipes.

DCS = ddc_service.o ddc_i2caux_helper.o

AMD_DAL_DCS = $(addprefix $(AMDDALPATH)/dc/dcs/,$(DCS))

AMD_DAL_FILES += $(AMD_DAL_DCS)

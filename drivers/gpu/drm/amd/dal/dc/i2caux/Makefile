#
# Makefile for the 'i2c' sub-component of DAL.
# It provides the control and status of HW i2c engine of the adapter.

I2CAUX = aux_engine.o engine_base.o i2caux.o i2c_engine.o \
	 i2c_generic_hw_engine.o i2c_hw_engine.o i2c_sw_engine.o

AMD_DAL_I2CAUX = $(addprefix $(AMDDALPATH)/dc/i2caux/,$(I2CAUX))

AMD_DAL_FILES += $(AMD_DAL_I2CAUX)

###############################################################################
# DCE 8x family
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE8_0
I2CAUX_DCE80 = i2caux_dce80.o i2c_hw_engine_dce80.o \
	i2c_sw_engine_dce80.o aux_engine_dce80.o

AMD_DAL_I2CAUX_DCE80 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce80/,$(I2CAUX_DCE80))

AMD_DAL_FILES += $(AMD_DAL_I2CAUX_DCE80)
endif

###############################################################################
# DCE 11x family
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
I2CAUX_DCE110 = i2caux_dce110.o i2c_sw_engine_dce110.o i2c_hw_engine_dce110.o \
	aux_engine_dce110.o

AMD_DAL_I2CAUX_DCE110 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce110/,$(I2CAUX_DCE110))

AMD_DAL_FILES += $(AMD_DAL_I2CAUX_DCE110)
endif

###############################################################################
# Diagnostics on FPGA
###############################################################################
I2CAUX_DIAG = i2caux_diag.o

AMD_DAL_I2CAUX_DIAG = $(addprefix $(AMDDALPATH)/dc/i2caux/diagnostics/,$(I2CAUX_DIAG))

AMD_DAL_FILES += $(AMD_DAL_I2CAUX_DIAG)


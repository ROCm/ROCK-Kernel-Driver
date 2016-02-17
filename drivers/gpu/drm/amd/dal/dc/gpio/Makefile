#
# Makefile for the 'gpio' sub-component of DAL.
# It provides the control and status of HW GPIO pins.

GPIO = ddc.o gpio_base.o gpio_service.o hw_ddc.o hw_factory.o \
       hw_gpio.o hw_gpio_pad.o hw_gpio_pin.o hw_hpd.o hw_translate.o irq.o

AMD_DAL_GPIO = $(addprefix $(AMDDALPATH)/dc/gpio/,$(GPIO))

AMD_DAL_FILES += $(AMD_DAL_GPIO)

###############################################################################
# DCE 8x
###############################################################################
# all DCE8.x are derived from DCE8.0
ifdef CONFIG_DRM_AMD_DAL_DCE8_0
GPIO_DCE80 = hw_translate_dce80.o hw_factory_dce80.o \
	hw_ddc_dce80.o hw_hpd_dce80.o

AMD_DAL_GPIO_DCE80 = $(addprefix $(AMDDALPATH)/dc/gpio/dce80/,$(GPIO_DCE80))

AMD_DAL_FILES += $(AMD_DAL_GPIO_DCE80)
endif

###############################################################################
# DCE 11x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
GPIO_DCE110 = hw_translate_dce110.o hw_factory_dce110.o hw_hpd_dce110.o \
	hw_ddc_dce110.o

AMD_DAL_GPIO_DCE110 = $(addprefix $(AMDDALPATH)/dc/gpio/dce110/,$(GPIO_DCE110))

AMD_DAL_FILES += $(AMD_DAL_GPIO_DCE110)
endif

###############################################################################
# Diagnostics on FPGA
###############################################################################
GPIO_DIAG_FPGA = hw_translate_diag.o hw_factory_diag.o hw_hpd_diag.o hw_ddc_diag.o

AMD_DAL_GPIO_DIAG_FPGA = $(addprefix $(AMDDALPATH)/dc/gpio/diagnostics/,$(GPIO_DIAG_FPGA))

AMD_DAL_FILES += $(AMD_DAL_GPIO_DIAG_FPGA)

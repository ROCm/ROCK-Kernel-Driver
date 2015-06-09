#
# Makefile for the 'gpio' sub-component of DAL.
# It provides the control and status of HW GPIO pins.

GPIO = gpio_service.o gpio.o ddc.o dvo.o irq.o hw_translate.o \
	hw_factory.o hw_gpio_pin.o hw_gpio.o hw_gpio_pad.o \
	hw_ddc.o hw_dvo.o hw_hpd.o

AMD_DAL_GPIO = $(addprefix $(AMDDALPATH)/gpio/,$(GPIO))

AMD_DAL_FILES += $(AMD_DAL_GPIO)

###############################################################################
# DCE 11x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
GPIO_DCE110 = hw_translate_dce110.o hw_factory_dce110.o hw_hpd_dce110.o \
	hw_ddc_dce110.o

AMD_DAL_GPIO_DCE110 = $(addprefix $(AMDDALPATH)/gpio/dce110/,$(GPIO_DCE110))

AMD_DAL_FILES += $(AMD_DAL_GPIO_DCE110)
endif


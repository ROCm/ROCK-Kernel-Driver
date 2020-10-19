#
# (c) Copyright 2020 Advanced Micro Devices, Inc. All the rights reserved
#
#  All rights reserved.  This notice is intended as a precaution against
#  inadvertent publication and does not imply publication or any waiver
#  of confidentiality.  The year included in the foregoing notice is the
#  year of creation of the work.
#
#  Authors: AMD
#
# Makefile for dcn302.
ifeq (1,$(AMDKCL_KBUILD_TAKE_PATH))
AMDKCL_KBUILD_TAKE_PATH_DCN302=$(AMDDALPATH)/dc/dcn302/
else
AMDKCL_KBUILD_TAKE_PATH_DCN302=
endif

DCN3_02 = dcn302_init.o dcn302_hwseq.o dcn302_resource.o

ifdef CONFIG_X86
CFLAGS_$(AMDKCL_KBUILD_TAKE_PATH_DCN302)dcn302_resource.o := -mhard-float -msse
endif

ifdef CONFIG_PPC64
CFLAGS_$(AMDKCL_KBUILD_TAKE_PATH_DCN302)dcn302_resource.o := -mhard-float -maltivec
endif

ifdef CONFIG_ARM64
CFLAGS_REMOVE_$(AMDKCL_KBUILD_TAKE_PATH_DCN302)dcn302_resource.o := -mgeneral-regs-only
endif

ifdef CONFIG_CC_IS_GCC
ifeq ($(call cc-ifversion, -lt, 0701, y), y)
IS_OLD_GCC = 1
endif
endif

ifdef CONFIG_X86
ifdef IS_OLD_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
CFLAGS_$(AMDKCL_KBUILD_TAKE_PATH_DCN302)dcn302_resource.o += -mpreferred-stack-boundary=4
else
CFLAGS_$(AMDKCL_KBUILD_TAKE_PATH_DCN302)dcn302_resource.o += -msse2
endif
endif

AMD_DAL_DCN3_02 = $(addprefix $(AMDDALPATH)/dc/dcn302/,$(DCN3_02))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN3_02)

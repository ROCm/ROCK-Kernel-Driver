DRM_VER=$(shell sed -n 's/^RHEL_DRM_VERSION = \(.*\)/\1/p' $(srctree)/Makefile)
DRM_PATCH=$(shell sed -n 's/^RHEL_DRM_PATCHLEVEL = \(.*\)/\1/p' $(srctree)/Makefile)
ifeq ($(DRM_VER),)
DRM_VER = $(VERSION)
DRM_PATCH = $(PATCHLEVEL)
endif

subdir-ccflags-y += \
	-DDRM_VER=$(DRM_VER) \
	-DDRM_PATCH=$(DRM_PATCH) \
	-DDRM_SUB="0"

LINUXINCLUDE := \
	-I$(src)/include \
	-I$(src)/include/uapi \
	-include $(src)/include/kcl/kcl_version.h \
	-include $(src)/include/rename_symbol.h \
	$(LINUXINCLUDE)

export CONFIG_DRM_TTM=m
export CONFIG_DRM_AMDGPU=m
export CONFIG_DRM_SCHED=m
export CONFIG_DRM_AMDGPU_CIK=y
export CONFIG_DRM_AMDGPU_SI=y
export CONFIG_DRM_AMDGPU_USERPTR=y
export CONFIG_DRM_AMD_DC=y
export CONFIG_DRM_AMD_DC_DCN1_0=y

subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_CIK
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_SI
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_USERPTR
subdir-ccflags-y += -DCONFIG_DRM_AMD_DC
subdir-ccflags-y += -DCONFIG_DRM_AMD_DC_DCN1_0

obj-m += scheduler/ amd/amdgpu/ ttm/ amd/amdkcl/

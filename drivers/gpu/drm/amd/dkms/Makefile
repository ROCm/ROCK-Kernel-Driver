ifneq (,$(wildcard /etc/os-release))
OS_NAME = "$(shell sed -n 's/^ID=\(.*\)/\1/p' /etc/os-release | tr -d '\"')"
OS_VERSION = $(shell sed -n 's/^VERSION_ID=\(.*\)/\1/p' /etc/os-release)
else
OS_NAME = "unknown"
OS_VERSION = "0.0"
endif

ifeq ("ubuntu",$(OS_NAME))
subdir-ccflags-y += -DOS_NAME_UBUNTU
else
subdir-ccflags-y += -DOS_NAME_UNKNOWN
endif

subdir-ccflags-y += \
	-DOS_VERSION_MAJOR=$(shell echo $(OS_VERSION).0 | cut -d. -f1) \
	-DOS_VERSION_MINOR=$(shell echo $(OS_VERSION).0 | cut -d. -f2)

ifeq ($(OS_NAME),"ubuntu")
OS_BUILD_NUM = $(shell echo $(KERNELRELEASE) | cut -d '-' -f 2)
subdir-ccflags-y += -DUBUNTU_BUILD_NUM=$(OS_BUILD_NUM)
ifeq ($(OS_VERSION),"14.04")
subdir-ccflags-y += -DOS_NAME_UBUNTU_1404
else
subdir-ccflags-y += -DOS_NAME_UBUNTU_1604
endif
endif

DKMS_INCLUDE_PREFIX = \
	-I$(src)/include \
	-I$(src)/include/drm \
	-I$(src)/include/uapi \
	-include $(src)/include/rename_symbol.h

export OS_NAME OS_VERSION DKMS_INCLUDE_PREFIX

export CONFIG_DRM_TTM=m
export CONFIG_DRM_AMDGPU=m
export BUILD_AS_DKMS=y
export CONFIG_DRM_AMDGPU_CIK=y
export CONFIG_DRM_AMDGPU_SI=y
export CONFIG_DRM_AMDGPU_USERPTR=y
export CONFIG_DRM_AMD_DC=y

subdir-ccflags-y += -DBUILD_AS_DKMS
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_CIK
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_SI
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_USERPTR
subdir-ccflags-y += -DCONFIG_DRM_AMD_DC=y

obj-m += amd/amdgpu/ ttm/ amd/amdkcl/

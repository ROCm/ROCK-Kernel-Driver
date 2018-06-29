ifneq (,$(wildcard /etc/os-release))
OS_NAME = "$(shell sed -n 's/^ID=\(.*\)/\1/p' /etc/os-release | tr -d '\"')"
OS_VERSION = $(shell sed -n 's/^VERSION_ID=\(.*\)/\1/p' /etc/os-release)
else ifneq (,$(wildcard /etc/redhat-release))
OS_NAME = "rhel"
OS_VERSION = "$(shell sed -n -e 's/[^0-9]*\([0-9]*.[0-9]*\).*/\1/p' /etc/redhat-release)"
else
OS_NAME = "unknown"
OS_VERSION = "0.0"
endif

ifeq ("centos",$(OS_NAME))
OS_NAME = "rhel"
OS_VERSION = "$(shell sed -n -e 's/[^0-9]*\([0-9]*.[0-9]*\).*/\1/p' /etc/centos-release-upstream)"
endif

ifeq ("ubuntu",$(OS_NAME))
subdir-ccflags-y += -DOS_NAME_UBUNTU
else ifeq ("rhel",$(OS_NAME))
subdir-ccflags-y += -DOS_NAME_RHEL
else ifeq ("steamos",$(OS_NAME))
subdir-ccflags-y += -DOS_NAME_STEAMOS
else ifeq ("opensuse",$(OS_NAME))
subdir-ccflags-y += -DOS_NAME_SLE
else ifeq ("sled",$(OS_NAME))
subdir-ccflags-y += -DOS_NAME_SLE
else ifeq ("sles",$(OS_NAME))
subdir-ccflags-y += -DOS_NAME_SLE
else ifeq ("amzn",$(OS_NAME))
subdir-ccflags-y += -DOS_NAME_AMZ
else
subdir-ccflags-y += -DOS_NAME_UNKNOWN
endif

subdir-ccflags-y += \
	-DOS_VERSION_MAJOR=$(shell echo $(OS_VERSION).0 | cut -d. -f1) \
	-DOS_VERSION_MINOR=$(shell echo $(OS_VERSION).0 | cut -d. -f2)

ifeq ($(OS_NAME),"opensuse")
ifeq ($(OS_VERSION),"42.3")
subdir-ccflags-y += -DOS_NAME_SUSE_42_3
endif
endif

ifeq ($(OS_NAME),"sled")
ifeq ($(OS_VERSION),"12.3")
subdir-ccflags-y += -DOS_NAME_SLE_12_3
endif
endif

ifeq ($(OS_NAME),"sles")
ifeq ($(OS_VERSION),"12.3")
subdir-ccflags-y += -DOS_NAME_SLE_12_3
endif
endif

ifeq ($(OS_NAME),"ubuntu")
OS_BUILD_NUM = $(shell echo $(KERNELRELEASE) | cut -d '-' -f 2)
subdir-ccflags-y += -DUBUNTU_BUILD_NUM=$(OS_BUILD_NUM)
ifeq ($(OS_VERSION),"14.04")
subdir-ccflags-y += -DOS_NAME_UBUNTU_1404
else
subdir-ccflags-y += -DOS_NAME_UBUNTU_1604
endif
endif

BUILD_DC = 1

ifeq ($(OS_NAME),"rhel")
ifeq ($(OS_VERSION),"6.8")
subdir-ccflags-y += -DOS_NAME_RHEL_6
else ifeq ($(OS_VERSION),"6.9")
subdir-ccflags-y += -DOS_NAME_RHEL_6
else ifeq ($(OS_VERSION),"6.10")
subdir-ccflags-y += -DOS_NAME_RHEL_6
else ifeq ($(OS_VERSION),"7.2")
subdir-ccflags-y += -DOS_NAME_RHEL_7_2
BUILD_DC = 0
else ifeq ($(OS_VERSION),"7.3")
subdir-ccflags-y += -DOS_NAME_RHEL_7_3
else ifeq ($(OS_VERSION),"7.4")
subdir-ccflags-y += -DOS_NAME_RHEL_7_4_5
subdir-ccflags-y += -DOS_NAME_RHEL_7_4
else ifeq ($(OS_VERSION),"7.5")
subdir-ccflags-y += -DOS_NAME_RHEL_7_4_5
subdir-ccflags-y += -DOS_NAME_RHEL_7_5
endif
subdir-ccflags-y += \
	-include /usr/src/kernels/$(KERNELRELEASE)/include/drm/drm_backport.h
endif

DKMS_INCLUDE_PREFIX = \
	-I$(src)/include \
	-I$(src)/include/drm \
	-I$(src)/include/uapi \
	-include $(src)/include/rename_symbol.h

export OS_NAME OS_VERSION DKMS_INCLUDE_PREFIX

export CONFIG_HSA_AMD=m
export CONFIG_DRM_TTM=m
export CONFIG_DRM_AMDGPU=m
export CONFIG_CHASH=m
export CONFIG_DRM_SCHED=m
export BUILD_AS_DKMS=y
export CONFIG_DRM_AMDGPU_CIK=y
export CONFIG_DRM_AMDGPU_SI=y
export CONFIG_DRM_AMDGPU_USERPTR=y
ifeq ($(BUILD_DC), 1)
export CONFIG_DRM_AMD_DC=y
export CONFIG_DRM_AMD_DC_DCN1_0=y
endif

subdir-ccflags-y += -DCONFIG_HSA_AMD
subdir-ccflags-y += -DBUILD_AS_DKMS
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_CIK
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_SI
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_USERPTR
ifeq ($(BUILD_DC), 1)
subdir-ccflags-y += -DCONFIG_DRM_AMD_DC
subdir-ccflags-y += -DCONFIG_DRM_AMD_DC_DCN1_0
endif

obj-m += amd/lib/ scheduler/ amd/amdgpu/ ttm/ amd/amdkcl/  amd/amdkfd/

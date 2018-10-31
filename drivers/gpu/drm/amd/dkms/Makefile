RHEL_MAJOR = $(shell sed -n 's/^RHEL_MAJOR = \(.*\)/\1/p' $(kdir)/Makefile)
RHEL_MINOR = $(shell sed -n 's/^RHEL_MINOR = \(.*\)/\1/p' $(kdir)/Makefile)

ifneq (,$(RHEL_MAJOR))
OS_NAME = "rhel"
OS_VERSION = "$(RHEL_MAJOR).$(RHEL_MINOR)"
else ifneq (,$(wildcard /etc/os-release))
OS_NAME = "$(shell sed -n 's/^ID=\(.*\)/\1/p' /etc/os-release | tr -d '\"')"
# On CentOS/RHEL, users could have installed a kernel not distributed from RHEL
ifeq ("centos",$(OS_NAME))
OS_NAME="custom-rhel"
else ifeq ("rhel",$(OS_NAME))
OS_NAME="custom-rhel"
else
OS_VERSION = $(shell sed -n 's/^VERSION_ID=\(.*\)/\1/p' /etc/os-release)
endif
else
OS_NAME = "unknown"
OS_VERSION = "0.0"
endif

ifneq ($(findstring $(OS_NAME), "ubuntu" "sled" "sles" "opensuse" "opensuse-leap" "amzn" "custom-rhel"),)
DRM_VER=$(shell sed -n 's/^VERSION = \(.*\)/\1/p' $(kdir)/Makefile)
DRM_PATCH=$(shell sed -n 's/^PATCHLEVEL = \(.*\)/\1/p' $(kdir)/Makefile)
else ifeq ("rhel",$(OS_NAME))
DRM_VER=$(shell sed -n 's/^RHEL_DRM_VERSION = \(.*\)/\1/p' $(kdir)/Makefile)
DRM_PATCH=$(shell sed -n 's/^RHEL_DRM_PATCHLEVEL = \(.*\)/\1/p' $(kdir)/Makefile)
endif

subdir-ccflags-y += \
	-DDRM_VER=$(DRM_VER) \
	-DDRM_PATCH=$(DRM_PATCH) \
	-DDRM_SUB="0"

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

ifeq ($(OS_NAME),"opensuse-leap")
ifeq ($(OS_VERSION),"15.0")
subdir-ccflags-y += -DOS_NAME_SUSE_15
endif
endif

ifeq ($(OS_NAME),"sled")
ifeq ($(OS_VERSION),"15")
subdir-ccflags-y += -DOS_NAME_SUSE_15
endif
endif

ifeq ($(OS_NAME),"sles")
ifeq ($(OS_VERSION),"15")
subdir-ccflags-y += -DOS_NAME_SUSE_15
endif
endif

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

ifeq ($(OS_NAME),"rhel")
ifeq ($(OS_VERSION),"6.8")
subdir-ccflags-y += -DOS_NAME_RHEL_6
else ifeq ($(OS_VERSION),"6.9")
subdir-ccflags-y += -DOS_NAME_RHEL_6
else ifeq ($(OS_VERSION),"6.10")
subdir-ccflags-y += -DOS_NAME_RHEL_6
else ifeq ($(OS_VERSION),"7.2")
subdir-ccflags-y += -DOS_NAME_RHEL_7_2
else ifeq ($(OS_VERSION),"7.3")
subdir-ccflags-y += -DOS_NAME_RHEL_7_3
else ifeq ($(OS_VERSION),"7.4")
subdir-ccflags-y += -DOS_NAME_RHEL_7_4
subdir-ccflags-y += -DOS_NAME_RHEL_7_X
else ifeq ($(OS_VERSION),"7.5")
subdir-ccflags-y += -DOS_NAME_RHEL_7_5
subdir-ccflags-y += -DOS_NAME_RHEL_7_X
else ifeq ($(OS_VERSION),"7.6")
subdir-ccflags-y += -DOS_NAME_RHEL_7_6
subdir-ccflags-y += -DOS_NAME_RHEL_7_X
endif
subdir-ccflags-y += \
	-include /usr/src/kernels/$(KERNELRELEASE)/include/drm/drm_backport.h
endif

DKMS_INCLUDE_PREFIX = \
	-I$(src)/include \
	-I$(src)/include/drm \
	-I$(src)/include/uapi \
	-include $(src)/include/kcl/kcl_version.h \
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
export CONFIG_DRM_AMD_DC=y
export CONFIG_DRM_AMD_DC_DCN1_0=y
export CONFIG_DRM_AMD_DC_DCN1_01=y

subdir-ccflags-y += -DCONFIG_HSA_AMD
subdir-ccflags-y += -DBUILD_AS_DKMS
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_CIK
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_SI
subdir-ccflags-y += -DCONFIG_DRM_AMDGPU_USERPTR
subdir-ccflags-y += -DCONFIG_DRM_AMD_DC
subdir-ccflags-y += -DCONFIG_DRM_AMD_DC_DCN1_0
subdir-ccflags-y += -DCONFIG_DRM_AMD_DC_DCN1_01

obj-m += amd/lib/ scheduler/ amd/amdgpu/ ttm/ amd/amdkcl/

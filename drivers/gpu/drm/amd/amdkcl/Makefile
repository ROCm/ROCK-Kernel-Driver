# SPDX-License-Identifier: MIT
amdkcl-y += main.o symbols.o kcl_common.o

amdkcl-y += kcl_workqueue.o kcl_backlight.o

ccflags-y += -include $(src)/../dkms/config/config.h
ccflags-y += -DHAVE_CONFIG_H

obj-m += amdkcl.o

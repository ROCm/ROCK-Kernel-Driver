# Makefile for AppArmor Linux Security Module (previously called "SubDomain")
#
APPARMOR_VER=2.0-19.43r6152

EXTRA_CFLAGS += -DAPPARMOR_VERSION=\"${APPARMOR_VER}\"

subdir-$(CONFIG_SECURITY_APPARMOR) += aamatch
obj-$(CONFIG_SECURITY_APPARMOR) += apparmor.o

apparmor-y := main.o list.o procattr.o lsm.o apparmorfs.o capabilities.o module_interface.o apparmor_version.o

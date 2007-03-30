# Makefile for AppArmor Linux Security Module
#
obj-$(CONFIG_SECURITY_APPARMOR) += apparmor.o

apparmor-y := main.o list.o procattr.o lsm.o apparmorfs.o \
	      module_interface.o match.o

quiet_cmd_make-caps = GEN     $@
cmd_make-caps = sed -n -e "/CAP_FS_MASK/d" -e "s/^\#define[ \\t]\\+CAP_\\([A-Z0-9_]\\+\\)[ \\t]\\+\\([0-9]\\+\\)\$$/[\\2]  = \"\\1\",/p" $< | tr A-Z a-z > $@

$(obj)/main.o : $(obj)/capability_names.h
$(obj)/capability_names.h : $(srctree)/include/linux/capability.h
	$(call cmd,make-caps)

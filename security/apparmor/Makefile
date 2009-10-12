# Makefile for AppArmor Linux Security Module
#
obj-$(CONFIG_SECURITY_APPARMOR) += apparmor.o

apparmor-y := apparmorfs.o audit.o capability.o context.o ipc.o lib.o match.o \
              path.o domain.o policy.o policy_interface.o procattr.o lsm.o \
              resource.o sid.o file.o

apparmor-$(CONFIG_SECURITY_APPARMOR_NETWORK) += net.o

clean-files: capability_names.h af_names.h

quiet_cmd_make-caps = GEN     $@
cmd_make-caps = echo "static const char *capability_names[] = {" > $@ ; sed -n -e "/CAP_FS_MASK/d" -e "s/^\#define[ \\t]\\+CAP_\\([A-Z0-9_]\\+\\)[ \\t]\\+\\([0-9]\\+\\)\$$/[\\2]  = \"\\1\",/p" $< | tr A-Z a-z >> $@ ; echo "};" >> $@

quiet_cmd_make-af = GEN     $@
cmd_make-af = echo "static const char *address_family_names[] = {" > $@ ; sed -n -e "/AF_MAX/d" -e "/AF_LOCAL/d" -e "s/^\#define[ \\t]\\+AF_\\([A-Z0-9_]\\+\\)[ \\t]\\+\\([0-9]\\+\\)\\(.*\\)\$$/[\\2]  = \"\\1\",/p" $< | tr A-Z a-z >> $@ ; echo "};" >> $@

$(obj)/capability.o : $(obj)/capability_names.h
$(obj)/net.o : $(obj)/af_names.h
$(obj)/capability_names.h : $(srctree)/include/linux/capability.h
	$(call cmd,make-caps)
$(obj)/af_names.h : $(srctree)/include/linux/socket.h
	$(call cmd,make-af)

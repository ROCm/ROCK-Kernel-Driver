
OBJCOPYFLAGS := -g --strip-unneeded

vmlinuz: vmlinux-stripped FORCE
	$(call if_changed,gzip)

vmlinux-stripped: vmlinux FORCE
	$(call if_changed,objcopy)

XINSTALL_NAME ?= $(KERNELRELEASE)
install: vmlinuz
	mkdir -p $(INSTALL_PATH)/boot
	ln -f -s vmlinuz-$(XINSTALL_NAME)$(INSTALL_SUFFIX) $(INSTALL_PATH)/boot/vmlinuz-$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(XENGUEST)$(INSTALL_SUFFIX)
	rm -f $(INSTALL_PATH)/boot/vmlinuz-$(XINSTALL_NAME)$(INSTALL_SUFFIX)
	install -m0644 vmlinuz $(INSTALL_PATH)/boot/vmlinuz-$(XINSTALL_NAME)$(INSTALL_SUFFIX)
	install -m0644 vmlinux $(INSTALL_PATH)/boot/vmlinux-syms-$(XINSTALL_NAME)$(INSTALL_SUFFIX)
	install -m0664 .config $(INSTALL_PATH)/boot/config-$(XINSTALL_NAME)$(INSTALL_SUFFIX)
	install -m0664 System.map $(INSTALL_PATH)/boot/System.map-$(XINSTALL_NAME)$(INSTALL_SUFFIX)
	ln -f -s vmlinuz-$(XINSTALL_NAME)$(INSTALL_SUFFIX) $(INSTALL_PATH)/boot/vmlinuz-$(VERSION).$(PATCHLEVEL)$(XENGUEST)$(INSTALL_SUFFIX)
	mkdir -p $(INSTALL_PATH)/usr/include/xen/linux
	install -m0644 $(srctree)/include/asm-xen/linux-public/*.h $(INSTALL_PATH)/usr/include/xen/linux

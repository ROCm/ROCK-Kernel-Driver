
include arch/$(ARCH)/Makefile

obj-y := initramfs_data.o

host-progs := gen_init_cpio

clean-files := initramfs_data.cpio.gz

$(obj)/initramfs_data.o: $(obj)/initramfs_data.cpio.gz
	$(OBJCOPY) $(ARCHBLOBLFLAGS) \
		--rename-section .data=.init.initramfs \
		$(obj)/initramfs_data.cpio.gz $(obj)/initramfs_data.o
	$(STRIP) -s $(obj)/initramfs_data.o

$(obj)/initramfs_data.cpio.gz: $(obj)/gen_init_cpio
	( cd $(obj) ; ./gen_init_cpio | gzip -9c > initramfs_data.cpio.gz )



obj-y := initramfs_data.o

host-progs := gen_init_cpio

clean-files := initramfs_data.cpio.gz

LDFLAGS_initramfs_data.o := $(LDFLAGS_BLOB) -r -T

$(obj)/initramfs_data.o: $(src)/initramfs_data.scr $(obj)/initramfs_data.cpio.gz FORCE
	$(call if_changed,ld)

$(obj)/initramfs_data.cpio.gz: $(obj)/gen_init_cpio
	./$< | gzip -9c > $@




obj-y := initramfs_data.o

host-progs  := gen_init_cpio

clean-files := initramfs_data.cpio.gz

LDFLAGS_initramfs_data.o := $(LDFLAGS_BLOB) -r -T

$(obj)/initramfs_data.o: $(src)/initramfs_data.scr \
			 $(obj)/initramfs_data.cpio.gz FORCE
	$(call if_changed,ld)

# initramfs-y are the programs which will be copied into the CPIO
# archive. Currently, the filenames are hardcoded in gen_init_cpio,
# but we need the information for the build as well, so it's duplicated
# here.

# Commented out for now
# initramfs-y := $(obj)/root/hello

quiet_cmd_cpio = CPIO    $@
      cmd_cpio = ./$< > $@

$(obj)/initramfs_data.cpio: $(obj)/gen_init_cpio $(initramfs-y) FORCE
	$(call if_changed,cpio)

targets += initramfs_data.cpio

$(obj)/initramfs_data.cpio.gz: $(obj)/initramfs_data.cpio FORCE
	$(call if_changed,gzip)

targets += initramfs_data.cpio.gz


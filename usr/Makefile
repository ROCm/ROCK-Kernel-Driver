
obj-y := initramfs_data.o

host-progs  := gen_init_cpio

clean-files := initramfs_data.cpio.gz

$(src)/initramfs_data.S: $(obj)/initramfs_data.cpio.gz
	echo "	.section .init.ramfs,\"a\"" > $(src)/initramfs_data.S
	echo ".incbin \"usr/initramfs_data.cpio.gz\"" >> $(src)/initramfs_data.S

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


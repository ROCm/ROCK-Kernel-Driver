
obj-y := initramfs_data.o

hostprogs-y  := gen_init_cpio

clean-files := initramfs_data.cpio.gz initramfs_list

# If you want a different list of files in the initramfs_data.cpio
# then you can either overwrite the cpio_list in this directory
# or set INITRAMFS_LIST to another filename.
INITRAMFS_LIST := $(obj)/initramfs_list

# initramfs_data.o contains the initramfs_data.cpio.gz image.
# The image is included using .incbin, a dependency which is not
# tracked automatically.
$(obj)/initramfs_data.o: $(obj)/initramfs_data.cpio.gz FORCE

# initramfs-y are the programs which will be copied into the CPIO
# archive. Currently, the filenames are hardcoded in gen_init_cpio,
# but we need the information for the build as well, so it's duplicated
# here.

# Commented out for now
# initramfs-y := $(obj)/root/hello

filechk_initramfs_list = $(CONFIG_SHELL) \
 $(srctree)/scripts/gen_initramfs_list.sh $(CONFIG_INITRAMFS_SOURCE)
			   
$(obj)/initramfs_list: FORCE
	$(call filechk,initramfs_list)

quiet_cmd_cpio = CPIO    $@
      cmd_cpio = ./$< $(obj)/initramfs_list > $@

$(obj)/initramfs_data.cpio: $(obj)/gen_init_cpio \
                            $(initramfs-y) $(obj)/initramfs_list FORCE
	$(call if_changed,cpio)

targets += initramfs_data.cpio

$(obj)/initramfs_data.cpio.gz: $(obj)/initramfs_data.cpio FORCE
	$(call if_changed,gzip)

targets += initramfs_data.cpio.gz


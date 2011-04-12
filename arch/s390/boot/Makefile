#
# Makefile for the linux s390-specific parts of the memory manager.
#

COMPILE_VERSION := __linux_compile_version_id__$(shell hostname |  \
			tr -c '[0-9A-Za-z]' '_')__$(shell date | \
			tr -c '[0-9A-Za-z]' '_')_t


chk-option = $(shell if $(CC) $(CFLAGS) $(1) -S -o /dev/null -xc /dev/null \
	     > /dev/null 2>&1; then echo "$(1)"; fi ;)

# Remove possible '-g' from CFLAGS_KERNEL, since we want to use stabs
# debug format.
override CFLAGS_KERNEL := $(shell echo $(CFLAGS_KERNEL) | sed 's/-g//')
ccflags-y  := -DCOMPILE_VERSION=$(COMPILE_VERSION) -gstabs -I.
# Assume we don't need the flag if the compiler doesn't know about it
ccflags-y  += $(call chk-option,-fno-eliminate-unused-debug-types)


targets := image
targets += bzImage
subdir- := compressed
targets += kerntypes.o

$(obj)/image: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/bzImage: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/compressed/vmlinux: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

install: $(CONFIGURE) $(obj)/image
	sh -x  $(srctree)/$(obj)/install.sh $(KERNELRELEASE) $(obj)/image \
	      System.map "$(INSTALL_PATH)"

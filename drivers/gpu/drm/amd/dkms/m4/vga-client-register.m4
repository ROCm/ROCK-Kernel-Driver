dnl #
dnl # v5.13-rc3-1630-gbf44e8cecc03
dnl # vgaarb: don't pass a cookie to vga_client_register
dnl #
AC_DEFUN([AC_AMDGPU_VGA_CLIENT_REGISTER_NOT_PASS_COOKIE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/vgaarb.h>
			struct pci_dev;
		], [
			unsigned int (*callback)(struct pci_dev *, bool) = NULL;
			vga_client_register(NULL, callback);
		], [
			AC_DEFINE(HAVE_VGA_CLIENT_REGISTER_NOT_PASS_COOKIE, 1,
				[vga_client_register() don't pass a cookie])
		])
	])
])

dnl #
dnl # commit fa3e967fffaf267ccab7959429722da34e45ad77
dnl # vga_switcheroo: Use enum vga_switcheroo_client_id instead of int
dnl #
AC_DEFUN([AC_AMDGPU_VGA_SWITCHEROO_GET_CLIENT_ID], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/vga_switcheroo.h>
			int test_get_client_id(struct pci_dev *pdev) {
				return 0;
			}
		], [
			struct vga_switcheroo_handler *handler = NULL;
			handler->get_client_id = test_get_client_id;
		], [
			AC_DEFINE(HAVE_VGA_SWITCHEROO_GET_CLIENT_ID_RETURN_INT, 1,
				[vga_switcheroo_handler->get_client_id() return int])
		])
	])
])

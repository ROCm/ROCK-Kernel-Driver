dnl #
dnl # commit 0d69704ae348
dnl # gpu/vga_switcheroo: add driver control power feature. (v3)
dnl #
AC_DEFUN([AC_AMDGPU_VGA_SWITCHEROO_REGISTER_CLIENT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/vga_switcheroo.h>
		], [
			vga_switcheroo_register_client(NULL, (const struct vga_switcheroo_client_ops *)NULL, 0);
		], [
			AC_DEFINE(HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_P_P_B, 1,
				[vga_switcheroo_register_client() has p,p,b interface])
			AC_DEFINE(HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_OPS, 1,
				[struct vga_switcheroo_client_ops is available])
		], [
			dnl #
			dnl # commit 26ec685ff9d9
			dnl # vga_switcheroo: Introduce struct vga_switcheroo_client_ops
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <linux/vga_switcheroo.h>
			], [
				vga_switcheroo_register_client(NULL, (const struct vga_switcheroo_client_ops *)NULL);
			], [
				AC_DEFINE(HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_P_P, 1,
					[vga_switcheroo_register_client() has p,p interface])
				AC_DEFINE(HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_OPS, 1,
					[struct vga_switcheroo_client_ops is available])
			], [
				dnl #
				dnl # commit 8d608aa62952
				dnl # vga_switcheroo: add reprobe hook for fbcon to recheck connected outputs.
				dnl #
				AC_KERNEL_TRY_COMPILE([
					#include <linux/vga_switcheroo.h>
				], [
					vga_switcheroo_register_client(NULL, NULL, NULL, NULL);
				], [
					AC_DEFINE(HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_P_P_P_P, 1,
						[vga_switcheroo_register_client() has p,p,p,p interface])
				])
			])
		])
	])
])

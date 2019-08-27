dnl #
dnl # commit 0d69704ae348
dnl # gpu/vga_switcheroo: add driver control power feature. (v3)
dnl #
AC_DEFUN([AC_AMDGPU_VGA_SWITCHEROO_REGISTER_CLIENT], [
	AC_MSG_CHECKING([whether vga_switcheroo_register_client() with 3 args & struct vga_switcheroo_client_ops is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/vga_switcheroo.h>
	], [
		vga_switcheroo_register_client(NULL, (const struct vga_switcheroo_client_ops *)NULL, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_3ARGS_VGA_SWITCHEROO_CLIENT_OPS_VGA_SWITCHEROO_REGISTER_CLIENT, 1, [vga_switcheroo_register_client() with 3 args & struct vga_switcheroo_client_ops])
	], [
		AC_MSG_RESULT(no)
dnl #
dnl # commit 26ec685ff9d9
dnl # vga_switcheroo: Introduce struct vga_switcheroo_client_ops
dnl #
		AC_MSG_CHECKING([whether vga_switcheroo_register_client() with 2 args is available])
		AC_KERNEL_TRY_COMPILE([
			#include <linux/vga_switcheroo.h>
		], [
			vga_switcheroo_register_client(NULL, (const struct vga_switcheroo_client_ops *)NULL);
		], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_2ARGS_VGA_SWITCHEROO_REGISTER_CLIENT, 1, [vga_switcheroo_register_client() with 2 args])
		], [
			AC_MSG_RESULT(no)
dnl #
dnl # commit 8d608aa62952
dnl # vga_switcheroo: add reprobe hook for fbcon to recheck connected outputs.
dnl #
			AC_MSG_CHECKING([whether vga_switcheroo_register_client() with 4 args is available])
			AC_KERNEL_TRY_COMPILE([
				#include <linux/vga_switcheroo.h>
			], [
				vga_switcheroo_register_client(NULL, NULL, NULL, NULL);
			], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_4ARGS_VGA_SWITCHEROO_REGISTER_CLIENT, 1, [vga_switcheroo_register_client() with 4 args])
			], [
				AC_MSG_RESULT(no)
			])
		])
	])
])

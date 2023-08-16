dnl #
dnl # v5.1-12318-g7159dbdae3c5
dnl # i2c: core: improve return value handling of i2c_new_device and i2c_new_dummy
dnl #
AC_DEFUN([AC_AMDGPU_I2C_NEW_CLIENT_DEVICE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/i2c.h>
		],[		
			i2c_new_client_device(NULL, NULL);
		],[i2c_new_client_device], [drivers/i2c/i2c-core-base.c],[
			AC_DEFINE(HAVE_I2C_NEW_CLIENT_DEVICE, 1,
				[i2c_new_client_device() is enabled])
		])
	])
])

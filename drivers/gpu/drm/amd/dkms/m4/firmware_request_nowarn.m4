dnl #
dnl # v4.17-rc3-12-g7dcc01343e48
dnl # firmware: add firmware_request_nowarn() - load firmware without warnings
dnl #
AC_DEFUN([AC_AMDGPU_FIRMWARE_REQUEST_NOWARN], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/firmware.h>
		],[
			firmware_request_nowarn(NULL, NULL, NULL);
		],[firmware_request_nowarn], [drivers/base/firmware_loader/main.c],[
			AC_DEFINE(HAVE_FIRMWARE_REQUEST_NOWARN, 1,
				[firmware_request_nowarn() is available])
		])
	])
])
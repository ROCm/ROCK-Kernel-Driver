dnl # commit c868edf42b4db89907b467c92b7f035c8c1cb0e5
dnl # firmware loader: inform direct failure when udev loader is disabled
dnl # Now that the udev firmware loader is optional request_firmware()
dnl # will not provide any information on the kernel ring buffer if
dnl # direct firmware loading failed and udev firmware loading is disabled.
dnl # If no information is needed request_firmware_direct() should be used
dnl # for optional firmware, at which point drivers can take on the onus
dnl # over informing of any failures, if udev firmware loading is disabled
dnl # though we should at the very least provide some sort of information
dnl # as when the udev loader was enabled by default back in the days.
AC_DEFUN([AC_AMDGPU_REQUEST_FIRMWARE_DIRECT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/firmware.h>
		], [
			request_firmware_direct(NULL, NULL, NULL);
		], [
			AC_DEFINE(HAVE_REQUEST_FIRMWARE_DIRECT, 1,
				[request_firmware_direct() is available])
		])
	])
])

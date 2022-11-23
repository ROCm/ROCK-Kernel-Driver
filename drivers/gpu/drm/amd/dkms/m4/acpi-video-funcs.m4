dnl #
dnl # commit: v6.1-rc1-17-da11ef832972
dnl # drm/amdgpu: Don't register backlight when another
dnl # backlight should be used (v3) 

AC_DEFUN([AC_AMDGPU_ACPI_VIDEO_BACKLIGHT_USE_NATIVE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <acpi/video.h>
		], [
			acpi_video_backlight_use_native();
		], [
			AC_DEFINE(HAVE_ACPI_VIDEO_BACKLIGHT_USE_NATIVE, 1,
				[acpi_video_backlight_use_native() is available])
		])
	])
])


AC_DEFUN([AC_AMDGPU_ACPI_VIDEO_FUNCS], [
                AC_AMDGPU_ACPI_VIDEO_BACKLIGHT_USE_NATIVE
])

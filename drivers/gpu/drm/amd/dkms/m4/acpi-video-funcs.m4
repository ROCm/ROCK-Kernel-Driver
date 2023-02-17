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

dnl #
dnl # commit: v6.1-rc1-161-c0f50c5de93b
dnl # drm/amdgpu: Register ACPI video backlight when
dnl # skipping amdgpu backlight registration 

AC_DEFUN([AC_AMDGPU_ACPI_VIDEO_REGISTER_BACKLIGHT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <acpi/video.h>
		], [
			acpi_video_register_backlight();
		], [
			AC_DEFINE(HAVE_ACPI_VIDEO_REGISTER_BACKLIGHT, 1,
				[acpi_video_register_backlight() is available])
		])
	])
])

dnl #
dnl # commit: v6.1-1561-g0ba8892d86ad
dnl ACPI: video: Allow GPU drivers to report no panels
dnl
AC_DEFUN([AC_AMDGPU_ACPI_VIDEO_REPORT_NOLCD], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE_SYMBOL([
                        #include <acpi/video.h>
                ], [
                        acpi_video_report_nolcd();
                ], [acpi_video_report_nolcd], [drivers/acpi/acpi_video.c], [
                        AC_DEFINE(HAVE_ACPI_VIDEO_REPORT_NOLCD, 1,
                                [acpi_video_report_nolcd() is available])
                ])
        ])
])

AC_DEFUN([AC_AMDGPU_ACPI_VIDEO_FUNCS], [
		AC_AMDGPU_ACPI_VIDEO_BACKLIGHT_USE_NATIVE
		AC_AMDGPU_ACPI_VIDEO_REGISTER_BACKLIGHT
		AC_AMDGPU_ACPI_VIDEO_REPORT_NOLCD
])

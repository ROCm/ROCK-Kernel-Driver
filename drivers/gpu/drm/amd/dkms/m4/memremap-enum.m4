dnl #
dnl # commit f25cbb7a95a24ff9a2a3bebd308e303942ae6b2c
dnl # mm: add zone device coherent type memory support
dnl #
dnl # commit dd19e6d8ffaa1289d75d7833de97faf1b6b2c8e4
dnl # mm: add device coherent vma selection for memory migration
dnl #
AC_DEFUN([AC_AMDGPU_MEMORY_DEVICE_COHERENT], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
			#include <linux/memremap.h>
			#include <linux/migrate.h>
                ], [
                        int v, w;
                        v = MEMORY_DEVICE_COHERENT;
                        w = MIGRATE_VMA_SELECT_DEVICE_COHERENT;
                ], [
                        AC_DEFINE(HAVE_DEVICE_COHERENT, 1,
                                [MEMORY_DEVICE_COHERENT is availablea])
                ])
        ])
])


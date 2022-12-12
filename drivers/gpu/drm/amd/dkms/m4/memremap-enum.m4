dnl #
dnl # commit c907e0eb43a522de60fb651c011c553f87273222 
dnl # memremap: add MEMREMAP_WC flag
dnl #
AC_DEFUN([AC_AMDGPU_MEMREMAP_WC], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/io.h>
		], [
			int v = MEMREMAP_WC;
		], [
			AC_DEFINE(HAVE_MEMREMAP_WC, 1,
				[enum MAMREMAP_WC is availablea])
		])
	])
])

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
                        int v = MEMORY_DEVICE_COHERENT;
			int w = MIGRATE_VMA_SELECT_DEVICE_COHERENT;
                ], [
                        AC_DEFINE(HAVE_DEVICE_COHERENT, 1,
                                [MEMORY_DEVICE_COHERENT is availablea])
                ])
        ])
])


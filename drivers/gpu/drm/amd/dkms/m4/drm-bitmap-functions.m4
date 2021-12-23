dnl #
dnl # commit c42b65e363ce introduce this change
dnl # v4.17-3-gc42b65e363ce
dnl # bitmap: Add bitmap_alloc(), bitmap_zalloc() and bitmap_free()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_BITMAP_FUNCS], [
         AC_KERNEL_DO_BACKGROUND([
                 AC_KERNEL_CHECK_SYMBOL_EXPORT([bitmap_free], [linux/bitmap.h], [
                         AC_DEFINE(HAVE_BITMAP_FUNCS, 1, [bitmap_free() is available])
                 ])
         ])
])

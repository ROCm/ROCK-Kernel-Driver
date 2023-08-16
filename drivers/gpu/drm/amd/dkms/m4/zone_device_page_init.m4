dnl #
dnl # v6.0-rc3-597-g0dc45ca1ce18 mm/memremap.c: take a pgmap reference on page allocation
dnl # v6.0-rc3-596-gef233450898f mm: free device private pages have zero refcount
dnl # v5.17-rc4-75-g27674ef6c73f mm: remove the extra ZONE_DEVICE struct page refcount
dnl #
AC_DEFUN([AC_AMDGPU_ZONE_DEVICE_PAGE_INIT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <linux/memremap.h>
			], [
				zone_device_page_init(NULL);
			], [zone_device_page_init], [mm/memremap.c], [
				AC_DEFINE(HAVE_ZONE_DEVICE_PAGE_INIT, 1, 
					[zone_device_page_init() is available])
		])
	])
])

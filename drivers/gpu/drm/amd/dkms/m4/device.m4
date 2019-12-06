dnl #
dnl # commit v4.18-rc6-6-gf07d141fe943
dnl # dma-mapping: Generalise dma_32bit_limit flag
dnl #
AC_DEFUN([AC_AMDGPU_DEVICE],
	[AC_MSG_CHECKING([whether struct device->bus_dma_mask is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/device.h>
	], [
		struct device *dev = NULL;
		dev->bus_dma_mask = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_STRUCT_DEVICE_BUS_DMA_MASK, 1,
		[struct device->bus_dma_mask is available])
	],[
		AC_MSG_RESULT(no)
	])
])

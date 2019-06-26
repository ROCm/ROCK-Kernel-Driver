dnl #
dnl # commit 1c8f422059ae5da07db7406ab916203f9417e396
dnl # Author: Souptick Joarder <jrdr.linux@gmail.com>
dnl # Date: Thu Apr 5 16:25:23 2018 -0700
dnl # mm: change return type to vm_fault_t
dnl #
AC_DEFUN([AC_AMDGPU_2ARGS_VIRTUAL_MM_FAULT_FUNCTION],
	[AC_MSG_CHECKING([whether ttm_bo_vm_fault() wants 2 args])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mm.h>
        ], [
		int (*fault)(struct vm_area_struct *vma, struct vm_fault *vmf) = 0;
		struct vm_operations_struct ttm_bo_vm_ops;
		ttm_bo_vm_ops.fault = fault;
        ], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_VIRTUAL_MM_FAULT_FUNCTION, 1, [ttm_bo_vm_fault() wants 2 args])
	], [
		AC_MSG_RESULT(no)
	])
])

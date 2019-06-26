dnl #
dnl # commit v4.10-9602-g11bac8000449
dnl # mm, fs: reduce fault, page_mkwrite, and pfn_mkwrite to take only vmf
dnl #
AC_DEFUN([AC_AMDGPU_VM_OPERATIONS_STRUCT_FAULT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/mm.h>
		], [
			int (*fault)(struct vm_area_struct *vma, struct vm_fault *vmf) = 0;
			struct vm_operations_struct ttm_bo_vm_ops;
			ttm_bo_vm_ops.fault = fault;
		], [
			AC_DEFINE(HAVE_VM_OPERATIONS_STRUCT_FAULT_2ARG, 1,
				[vm_operations_struct->fault() wants 2 args])
		])
	])
])

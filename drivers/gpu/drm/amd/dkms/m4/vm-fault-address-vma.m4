dnl #
dnl # commit v4.9-7746-g82b0f8c39a38
dnl # mm: join struct fault_env and vm_fault
dnl #
AC_DEFUN([AC_AMDGPU_VM_FAULT_ADDRESS_VMA], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/mm.h>
		], [
			struct vm_fault *ptest = NULL;
			ptest->address = 0;
			ptest->vma = NULL;
		], [
			AC_DEFINE(HAVE_VM_FAULT_ADDRESS_VMA, 1,
				[vm_fault->{address/vam} is available])
		])
	])
])

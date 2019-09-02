dnl #
dnl # commit 82b0f8c39a3869b6fd2a10e180a862248736ec6f
dnl # mm: join struct fault_env and vm_fault
dnl #
AC_DEFUN([AC_AMDGPU_VM_FAULT_ADDRESS_VMA],
	[AC_MSG_CHECKING([whether vm_fault->{address/vam} is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		struct vm_fault *ptest = NULL;
		ptest->address = 0;
		ptest->vma = NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VM_FAULT_ADDRESS_VMA, 1, [vm_fault->{address/vam} is available])
	],[
		AC_MSG_RESULT(no)
	])
])

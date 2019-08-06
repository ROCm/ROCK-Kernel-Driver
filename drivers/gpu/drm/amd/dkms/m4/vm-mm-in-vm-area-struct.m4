dnl #
dnl # commit c92ff1bde06f69d59b40f3194016aee51cc5da55
dnl # Author: Martin Schwidefsky <schwidefsky@de.ibm.com>
dnl # Date:   Tue Oct 16 01:24:43 2007 -0700
dnl # move mm_struct and vm_area_struct
dnl #
AC_DEFUN([AC_AMDGPU_VM_MM_IN_STRUCT_VM_AREA_STRUCT],
	[AC_MSG_CHECKING([for vm_mm field within vm_area_struct structure])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mm_types.h>
	], [
		struct vm_area_struct vma;
		vma.vm_mm = 0;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VM_MM_IN_STRUCT_VM_AREA_STRUCT, 1, [vm_area_struct structure contains vm_mm field])
	], [
		AC_MSG_RESULT(no)
	])
])

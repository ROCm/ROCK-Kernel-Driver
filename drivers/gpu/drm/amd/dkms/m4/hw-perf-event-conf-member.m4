dnl #
dnl #commit cf25f904ef75aa7c25097eb4981bbc634bf5ff9e
dnl #Author: Suravee Suthikulpanit <suravee.suthikulpanit@amd.com>
dnl #Date:   Fri Feb 24 02:48:21 2017 -0600
dnl #x86/events/amd/iommu: Add IOMMU-specific hw_perf_event struct
dnl #
AC_DEFUN([AC_AMDGPU_HW_PERF_EVENT_CONF_MEMBER],
		[AC_MSG_CHECKING([whether hw_perf_event conf member is defined])
		AC_KERNEL_TRY_COMPILE([
				#include <linux/perf_event.h>
		],[
				struct hw_perf_event *hwc = NULL;
				hwc->conf = 0;
		],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_HW_PERF_EVENT_CONF_MEMBER, 1, [hw_perf_event conf member is defined])
		],[
				AC_MSG_RESULT(no)
		])
])


dnl #
dnl # commit ebc614f687369f9df99828572b1d85a7c2de3d92
dnl # bpf, cgroup: implement eBPF-based device controller for cgroup v2
dnl #
AC_DEFUN([AC_AMDGPU_BPF_CGROUP_RUN_PROG_DEVICE_CGROUP],
	[AC_MSG_CHECKING([whether BPF_CGROUP_RUN_PROG_DEVICE_CGROUP is defined])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/bpf-cgroup.h>
	], [
		#if !defined(BPF_CGROUP_RUN_PROG_DEVICE_CGROUP)
		#error BPF_CGROUP_RUN_PROG_DEVICE_CGROUP not #defined
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BPF_CGROUP_RUN_PROG_DEVICE_CGROUP, 1,
		[BPF_CGROUP_RUN_PROG_DEVICE_CGROUP is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

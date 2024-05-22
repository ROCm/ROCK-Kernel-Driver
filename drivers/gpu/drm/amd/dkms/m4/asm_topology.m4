dnl #
dnl # v6.8-rc4-70-gfd43b8ae76e9
dnl # x86/cpu/topology: Provide __num_[cores|threads]_per_package
dnl #
AC_DEFUN([AC_AMDGPU_TOPOLOGY_NUM_CORES_PER_PACKAGE], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
						#include <asm/cache.h>
						#include <asm/topology.h>
                ], [
						int a = 0;
						a = topology_num_cores_per_package();
                ], [
                        AC_DEFINE(HAVE_TOPOLOGY_NUM_CORES_PER_PACKAGE, 1,
                                [topology_num_cores_per_package is availablea])
                ])
        ])
])


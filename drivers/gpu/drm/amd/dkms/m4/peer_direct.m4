dnl #
dnl # 
dnl # PEER DIRECT support
dnl #
AC_DEFUN([AC_AMDGPU_KFD_PEERDIRECT_SUPPORT], [
    AC_KERNEL_DO_BACKGROUND([
        AS_IF([ grep -qw ib_register_peer_memory_client /usr/src/ofa_kernel/x86_64/${KERNELVER}/Module.symvers ], [
            AC_DEFINE(HAVE_KFD_PEERDIRECT_SUPPORT, 1, [HAVE_KFD_PEERDIRECT_SUPPORT is available])
        ])
    ])
])


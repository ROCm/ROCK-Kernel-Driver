dnl #
dnl # commit v6.9-rc1-17-g210a03c9d51a
dnl # fs: claw back a few FMODE_* bits
dnl #
AC_DEFUN([AC_AMDGPU_FILE_OPERATION_FOP_FLAGS], [
    AC_KERNEL_DO_BACKGROUND([
        AC_KERNEL_TRY_COMPILE([
            #include <linux/fs.h>
        ],[
            struct file_operations file_operation;
            file_operation.fop_flags = 0;
        ],[
            AC_DEFINE(HAVE_FILE_OPERATION_FOP_FLAGS, 1,
                [file_operation->fop_flags is available])
        ])
    ])
])

AC_DEFUN([AC_AMDGPU_STRUCT_FILE_OPERATION], [
    AC_AMDGPU_FILE_OPERATION_FOP_FLAGS
])

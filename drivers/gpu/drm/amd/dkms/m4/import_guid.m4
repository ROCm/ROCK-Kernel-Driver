dnl #
dnl # v5.6-rc7-127-gd01cd62400b3
dnl # uuid: Add inline helpers to import / export UUIDs
dnl #
AC_DEFUN([AC_AMDGPU_IMPORT_GUID], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/uuid.h>
                ],[
                        import_guid(NULL, NULL);
                ],[
                        AC_DEFINE(HAVE_IMPORT_GUID, 1,
                                [import_guid() is available])
                ])
        ])
])




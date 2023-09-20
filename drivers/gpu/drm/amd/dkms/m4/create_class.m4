dnl #
dnl # v6.3-rc1-13-g1aaba11da9aa driver core: class: remove module * from class_create()
dnl #
AC_DEFUN([AC_AMDGPU_LINUX_DEVICE_CLASS], [
	AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/device/class.h>
                ], [
                        struct class* class = NULL;
                        class = class_create(NULL);
                ], [
                        AC_DEFINE(HAVE_ONE_ARGUMENT_OF_CLASS_CREATE, 1,
                                [class_create has one argument])
                ]) 
        ])
])

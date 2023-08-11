dnl #
dnl # commit v6.0-rc3-595-g16ce101db85d
dnl # mm/memory.c: fix race when faulting a device private page
dnl #
AC_DEFUN([AC_AMDGPU_MIGRATE_VMA_FAULT_PAGE], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <linux/migrate.h>
                ], [
                        struct migrate_vma mig = {0};
                        struct page *fault_page = NULL;
                        mig.fault_page = fault_page;
                ], [
                        AC_DEFINE(HAVE_MIGRATE_VMA_FAULT_PAGE, 1,
                                [struct migrate_vma has fault_page])
                ])
        ])
])


dnl #
dnl # commit v6.6-rc1-4-gb9655e702dc5
dnl # x86/cpu: Encapsulate topology information in cpuinfo_x86
dnl #
AC_DEFUN([AC_AMDGPU_CPUINFO_X86], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <asm/processor.h>
                ],[
                        struct cpuinfo_x86* cpuinfo = NULL;
                        struct cpuinfo_topology topo;
                        topo = cpuinfo -> topo;
                ],[
                        AC_DEFINE(HAVE_CPUINFO_TOPOLOGY_IN_CPUINFO_X86_STRUCT, 1,
                                [ cpuinfo_x86.topo is available])
                ])
        ])
])

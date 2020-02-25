dnl #
dnl # Check whether linux/overflow.h is available
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_OVERFLOW_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/overflow.h], [
		AC_DEFINE(HAVE_OVERFLOW_H, 1, [linux/overflow.h is available])
	])
])

dnl #
dnl # Check whether linux/sched/mm.h is available
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_SCHED_MM_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/sched/mm.h], [
		AC_DEFINE(HAVE_MM_H, 1, [linux/sched/mm.h is available])
	])
])

dnl #
dnl # Check whether linux/sched/task.h is available
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_SCHED_TASK_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/sched/task.h], [
		AC_DEFINE(HAVE_TASK_H, 1, [linux/sched/task.h is available])
	])
])

dnl #
dnl # Check whether linux/sched/signal.h is available
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_SCHED_SIGNAL_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/sched/signal.h], [
		AC_DEFINE(HAVE_SIGNAL_H, 1, [linux/sched/signal.h is available])
	])
])

dnl #
dnl #  commit v4.15-28-gf3804203306e
dnl #  array_index_nospec: Sanitize speculative array de-references
dnl #
AC_DEFUN([AC_AMDGPU_NOSPEC_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/nospec.h], [
		AC_DEFINE(HAVE_LINUX_NOSPEC_H, 1, [linux/nospec.h is available])
	])
])

dnl #
dnl # commit 4201d9a8e86b51dd40aa8a0dabd093376c859985
dnl #    kfifo: add the new generic kfifo API
dnl #
AC_DEFUN([AC_AMDGPU_KFIFO_NEW_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/kfifo-new.h], [
		AC_DEFINE(HAVE_KFIFO_NEW_H, 1, [kfifo_new.h is available])
	])
])

dnl #
dnl #	commit 8bd9cb51daac89337295b6f037b0486911e1b408
dnl # locking/atomics, asm-generic: Move some macros from <linux/bitops.h> to a new <linux/bits.h> file
dnl #
AC_DEFUN([AC_AMDGPU_BITS_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/bits.h], [
		AC_DEFINE(HAVE_LINUX_BITS_H, 1,
			[whether linux/bits.h is available])
	])
])

dnl #
dnl # commit v4.3-rc4-1-g2f8e2c877784
dnl # move io-64-nonatomic*.h out of asm-generic
dnl #
AC_DEFUN([AC_AMDGPU_IO_64_NONATOMIC_LO_HI_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/io-64-nonatomic-lo-hi.h], [
		AC_DEFINE(HAVE_LINUX_IO_64_NONATOMIC_LO_HI_H, 1,
			[linux/io-64-nonatomic-lo-hi.h is available])
	])
])

dnl #
dnl # commit 299878bac326c890699c696ebba26f56fe93fc75
dnl # treewide: move set_memory_* functions away from cacheflush.h
dnl #
AC_DEFUN([AC_AMDGPU_ASM_SET_MEMORY_H], [
	AC_KERNEL_TMP_BUILD_DIR([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/kernel.h>
			#ifdef CONFIG_X86
			#include <asm/set_memory.h>
			#endif
		],[
			#ifndef CONFIG_X86
			#error just check arch/x86/include/asm/set_memory.h
			#endif
		],[
			AC_DEFINE(HAVE_SET_MEMORY_H, 1, [asm/set_memory.h is available])
		])
	])
])

dnl #
dnl # commit df6b35f409af0a8ff1ef62f552b8402f3fef8665
dnl # x86/fpu: Rename i387.h to fpu/api.h
dnl #
AC_DEFUN([AC_AMDGPU_ASM_FPU_API_H], [
	AC_KERNEL_TMP_BUILD_DIR([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/kernel.h>
			#ifdef CONFIG_X86
			#include <asm/fpu/api.h>
			#endif
		],[
			#ifndef CONFIG_X86
			#error just check arch/x86/include/asm/fpu/api.h
			#endif
		],[
			AC_DEFINE(HAVE_ASM_FPU_API_H, 1, [asm/fpu/api.h is available])
		])
	])
])

dnl #
dnl # commit 607ca46e97a1b6594b29647d98a32d545c24bdff
dnl # UAPI: (Scripted) Disintegrate include/linux
dnl #
AC_DEFUN([AC_AMDGPU_SCHED_TYPES_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([uapi/linux/sched/types.h], [
		AC_DEFINE(HAVE_SCHED_TYPES_H, 1, [sched/types.h is available])
	])
])

dnl # commit 9826a516ff77c5820e591211e4f3e58ff36f46be
dnl # Author: Michel Lespinasse <walken@google.com>
dnl # Date: Mon Oct 8 16:31:35 2012 -0700
dnl # mm: interval tree updates
dnl # Update the generic interval tree code that was introduced in
dnl # "mm:replace  vma prio_tree with an interval tree".
AC_DEFUN([AC_AMDGPU_MM_INTERVAL_TREE_DEFINE], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([linux/interval_tree_generic.h],[
		AC_DEFINE(HAVE_INTERVAL_TREE_DEFINE, 1,
			[whether INTERVAL_TREE_DEFINE() is defined])
	])
])

AC_DEFUN([AC_AMDGPU_LINUX_HEADERS], [
	AC_AMDGPU_OVERFLOW_H
	AC_AMDGPU_SCHED_MM_H
	AC_AMDGPU_SCHED_TASK_H
	AC_AMDGPU_SCHED_SIGNAL_H
	AC_AMDGPU_NOSPEC_H
	AC_AMDGPU_KFIFO_NEW_H
	AC_AMDGPU_BITS_H
	AC_AMDGPU_IO_64_NONATOMIC_LO_HI_H
	AC_AMDGPU_ASM_SET_MEMORY_H
	AC_AMDGPU_ASM_FPU_API_H
	AC_AMDGPU_SCHED_TYPES_H
	AC_AMDGPU_MM_INTERVAL_TREE_DEFINE
])

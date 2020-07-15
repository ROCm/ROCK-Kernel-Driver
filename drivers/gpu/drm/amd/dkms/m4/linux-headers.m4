AC_DEFUN([AC_AMDGPU_LINUX_HEADERS], [

	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/overflow.h])

	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/sched/mm.h])

	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/sched/task.h])

	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/sched/signal.h])

	dnl #
	dnl #  commit v4.15-28-gf3804203306e
	dnl #  array_index_nospec: Sanitize speculative array de-references
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/nospec.h])

	dnl #
	dnl # commit 4201d9a8e86b51dd40aa8a0dabd093376c859985
	dnl # kfifo: add the new generic kfifo API
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/kfifo-new.h])

	dnl #
	dnl # commit 8bd9cb51daac89337295b6f037b0486911e1b408
	dnl # locking/atomics, asm-generic: Move some macros from <linux/bitops.h>
	dnl # to a new <linux/bits.h> file
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/bits.h])

	dnl #
	dnl # commit v4.3-rc4-1-g2f8e2c877784
	dnl # move io-64-nonatomic*.h out of asm-generic
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/io-64-nonatomic-lo-hi.h])

	dnl #
	dnl # commit 299878bac326c890699c696ebba26f56fe93fc75
	dnl # treewide: move set_memory_* functions away from cacheflush.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([asm/set_memory.h])

	dnl #
	dnl # commit df6b35f409af0a8ff1ef62f552b8402f3fef8665
	dnl # x86/fpu: Rename i387.h to fpu/api.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([asm/fpu/api.h])

	dnl #
	dnl # commit 607ca46e97a1b6594b29647d98a32d545c24bdff
	dnl # UAPI: (Scripted) Disintegrate include/linux
	dnl #
	AC_KERNEL_CHECK_HEADERS([uapi/linux/sched/types.h])

	dnl # commit 9826a516ff77c5820e591211e4f3e58ff36f46be
	dnl # Author: Michel Lespinasse <walken@google.com>
	dnl # Date: Mon Oct 8 16:31:35 2012 -0700
	dnl # mm: interval tree updates
	dnl # Update the generic interval tree code that was introduced in
	dnl # "mm:replace  vma prio_tree with an interval tree".
	AC_KERNEL_CHECK_HEADERS([linux/interval_tree_generic.h])

	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/dma-fence.h])
])

AC_DEFUN([AC_AMDGPU_LINUX_HEADERS], [

	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/sched/task.h])

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
	dnl # v4.19-rc6-7-ga3f8a30f3f00
	dnl # Compiler Attributes: use feature checks instead of version checks
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/compiler_attributes.h])

	dnl #
	dnl # commit b3dfbdf261e076a997f812323edfdba84ba80256
	dnl # dma-buf/fence: add fence_array fences v6
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/fence-array.h])

	dnl #
	dnl # v5.3-rc1-449-g52791eeec1d9
	dnl $ dma-buf: rename reservation_object to dma_resv
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/dma-resv.h])

	dnl #
	dnl # v5.7-13149-g9740ca4e95b4
	dnl # mmap locking API: initial implementation as rwsem wrappers
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/mmap_lock.h])

	dnl #
	dnl # v4.19-rc4-1-g52916982af48
	dnl # PCI/P2PDMA: Support peer-to-peer memory
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/pci-p2pdma.h])

	dnl #
	dnl # v4.7-11546-g00085f1efa38
	dnl # dma-mapping: use unsigned long for dma_attrs
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/dma-attrs.h])

	dnl #
	dnl # 01fd30da0474
	dnl # dma-buf: Add struct dma-buf-map for storing struct dma_buf.vaddr_ptr
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/dma-buf-map.h])

	dnl #
	dnl # 7938f4218168
	dnl # dma-buf: dma-buf-map: Rename to iosys-map
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/iosys-map.h])

	dnl #
	dnl # v5.14-rc5-11-gc0891ac15f04
	dnl # isystem: ship and use stdarg.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/stdarg.h])

	dnl #
	dnl # v5.0-1331-g7bf60c52e093
	dnl # dma-buf: add new dma_fence_chain container v7
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/dma-fence-chain.h])

	dnl #
	dnl # v4.16-11455-gf6bb2a2c0b81
	dnl # xarray: add the xa_lock to the radix_tree_root
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/xarray.h])

	dnl #
	dnl # v5.15-272-gd2a8ebbf8192
	dnl # kernel.h: split out container_of() and typeof_member() macros
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/container_of.h])

	dnl #
	dnl # v5.15-rc4-2-g46b49b12f3fc
	dnl # arch/cc: Introduce a function to check for confidential computing features
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/cc_platform.h])

	dnl #
	dnl # v4.12-rc3-120-gfd851a3cdc19
	dnl # spin loop primitives for busy waiting
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/processor.h])

	dnl #
	dnl # v5.9-rc6-311-g0a0f0d8be76d
	dnl # dma-mapping: split <linux/dma-mapping.h>
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/dma-map-ops.h])

	dnl #
	dnl #v5.14-rc6-42-g089050cafa10
	dnl #rbtree: Split out the rbtree type definitions into <linux/rbtree_types.h>
	dnl
	AC_KERNEL_CHECK_HEADERS([linux/rbtree_types.h])
])

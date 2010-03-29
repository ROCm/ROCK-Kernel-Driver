/*
 * linux/mm/tmem.h
 *
 * Interface to transcendent memory, used by mm/precache.c and mm/preswap.c
 * Currently implemented on XEN, but may be implemented elsewhere in future.
 *
 * Copyright (C) 2008,2009 Dan Magenheimer, Oracle Corp.
 */

#ifdef CONFIG_XEN
#include <xen/interface/xen.h>

/* Bits for HYPERVISOR_tmem_op(TMEM_NEW_POOL) */
#define TMEM_POOL_MIN_PAGESHIFT   12
#define TMEM_POOL_PAGEORDER       (PAGE_SHIFT - TMEM_POOL_MIN_PAGESHIFT)

extern int xen_tmem_op(u32 tmem_cmd, u32 tmem_pool, u64 object, u32 index,
	unsigned long gmfn, u32 tmem_offset, u32 pfn_offset, u32 len);
extern int xen_tmem_new_pool(u32 tmem_cmd, u64 uuid_lo, u64 uuid_hi, u32 flags);

static inline int tmem_put_page(u32 pool_id, u64 object, u32 index,
	unsigned long gmfn)
{
	return xen_tmem_op(TMEM_PUT_PAGE, pool_id, object, index,
		gmfn, 0, 0, 0);
}

static inline int tmem_get_page(u32 pool_id, u64 object, u32 index,
	unsigned long gmfn)
{
	return xen_tmem_op(TMEM_GET_PAGE, pool_id, object, index,
		gmfn, 0, 0, 0);
}

static inline int tmem_flush_page(u32 pool_id, u64 object, u32 index)
{
	return xen_tmem_op(TMEM_FLUSH_PAGE, pool_id, object, index,
		0, 0, 0, 0);
}

static inline int tmem_flush_object(u32 pool_id, u64 object)
{
	return xen_tmem_op(TMEM_FLUSH_OBJECT, pool_id, object, 0, 0, 0, 0, 0);
}

static inline int tmem_new_pool(u64 uuid_lo, u64 uuid_hi, u32 flags)
{
	BUILD_BUG_ON((TMEM_POOL_PAGEORDER < 0) ||
		(TMEM_POOL_PAGEORDER >= TMEM_POOL_PAGESIZE_MASK));
	flags |= TMEM_POOL_PAGEORDER << TMEM_POOL_PAGESIZE_SHIFT;
	return xen_tmem_new_pool(TMEM_NEW_POOL, uuid_lo, uuid_hi, flags);
}

static inline int tmem_destroy_pool(u32 pool_id)
{
	return xen_tmem_op(TMEM_DESTROY_POOL, pool_id, 0, 0, 0, 0, 0, 0);
}
#else
struct tmem_op {
	u32 cmd;
	s32 pool_id; /* private > 0; shared < 0; 0 is invalid */
	union {
		struct {  /* for cmd == TMEM_NEW_POOL */
			u64 uuid[2];
			u32 flags;
		} new;
		struct {  /* for cmd == TMEM_CONTROL */
			u32 subop;
			u32 cli_id;
			u32 arg1;
			u32 arg2;
			void *buf;
		} ctrl;
		struct {
			u64 object;
			u32 index;
			u32 tmem_offset;
			u32 pfn_offset;
			u32 len;
			unsigned long pfn;  /* page frame */
		} gen;
	} u;
};
#endif

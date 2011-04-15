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

struct tmem_pool_uuid {
	u64 lo;
	u64 hi;
};

struct tmem_oid {
	u64 oid[3];
};

extern int xen_tmem_op(u32 tmem_cmd, u32 tmem_pool, struct tmem_oid, u32 index,
	unsigned long gmfn, u32 tmem_offset, u32 pfn_offset, u32 len);
extern int xen_tmem_new_pool(struct tmem_pool_uuid, u32 flags);

static inline int tmem_put_page(u32 pool_id, struct tmem_oid oid, u32 index,
	unsigned long gmfn)
{
	return xen_tmem_op(TMEM_PUT_PAGE, pool_id, oid, index,
		gmfn, 0, 0, 0);
}

static inline int tmem_get_page(u32 pool_id, struct tmem_oid oid, u32 index,
	unsigned long gmfn)
{
	return xen_tmem_op(TMEM_GET_PAGE, pool_id, oid, index,
		gmfn, 0, 0, 0);
}

static inline int tmem_flush_page(u32 pool_id, struct tmem_oid oid, u32 index)
{
	return xen_tmem_op(TMEM_FLUSH_PAGE, pool_id, oid, index,
		0, 0, 0, 0);
}

static inline int tmem_flush_object(u32 pool_id, struct tmem_oid oid)
{
	return xen_tmem_op(TMEM_FLUSH_OBJECT, pool_id, oid, 0, 0, 0, 0, 0);
}

static inline int tmem_new_pool(u64 uuid_lo, u64 uuid_hi, u32 flags)
{
	struct tmem_pool_uuid uuid = { .lo = uuid_lo, .hi = uuid_hi };

	BUILD_BUG_ON((TMEM_POOL_PAGEORDER < 0) ||
		(TMEM_POOL_PAGEORDER >= TMEM_POOL_PAGESIZE_MASK));
	flags |= TMEM_POOL_PAGEORDER << TMEM_POOL_PAGESIZE_SHIFT;
	return xen_tmem_new_pool(uuid, flags);
}

static inline int tmem_destroy_pool(u32 pool_id)
{
	static const struct tmem_oid oid = {};

	return xen_tmem_op(TMEM_DESTROY_POOL, pool_id, oid, 0, 0, 0, 0, 0);
}
#endif

/*
 * Xen implementation for transcendent memory (tmem)
 *
 * Dan Magenheimer <dan.magenheimer@oracle.com> 2009
 */

#include <linux/types.h>
#include <xen/interface/xen.h>
#include <asm/hypervisor.h>
#include "tmem.h"

int xen_tmem_op(u32 tmem_cmd, u32 tmem_pool, struct tmem_oid oid, u32 index,
	unsigned long gmfn, u32 tmem_offset, u32 pfn_offset, u32 len)
{
	struct tmem_op op;
	int rc = 0;

	op.cmd = tmem_cmd;
	op.pool_id = tmem_pool;
	BUILD_BUG_ON(sizeof(op.u.gen.oid) != sizeof(oid.oid));
	memcpy(op.u.gen.oid, oid.oid, sizeof(op.u.gen.oid));
	op.u.gen.index = index;
	op.u.gen.tmem_offset = tmem_offset;
	op.u.gen.pfn_offset = pfn_offset;
	op.u.gen.len = len;
	op.u.gen.cmfn = gmfn;
	rc = HYPERVISOR_tmem_op(&op);
	return rc;
}

int xen_tmem_new_pool(uint32_t tmem_cmd, struct tmem_pool_uuid uuid,
	uint32_t flags)
{
	struct tmem_op op;
	int rc = 0;

	op.cmd = tmem_cmd;
	op.u.creat.uuid[0] = uuid.lo;
	op.u.creat.uuid[1] = uuid.hi;
#ifdef TMEM_SPEC_VERSION
	switch (flags >> TMEM_POOL_VERSION_SHIFT) {
	case 0:
		flags |= TMEM_SPEC_VERSION << TMEM_POOL_VERSION_SHIFT;
		break;
	case TMEM_SPEC_VERSION:
		break;
	default:
		WARN(1, "TMEM: Bogus version %u, expecting %u\n",
		     flags >> TMEM_POOL_VERSION_SHIFT, TMEM_SPEC_VERSION);
		return -ENOSYS;
	}
#endif
	op.u.creat.flags = flags;
	rc = HYPERVISOR_tmem_op(&op);
	return rc;
}

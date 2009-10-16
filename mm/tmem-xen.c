/*
 * Xen implementation for transcendent memory (tmem)
 *
 * Dan Magenheimer <dan.magenheimer@oracle.com> 2009
 */

#include <linux/types.h>
#include <xen/interface/xen.h>
#include <asm/hypervisor.h>

int xen_tmem_op(u32 tmem_cmd, u32 tmem_pool, u64 object, u32 index,
	unsigned long gmfn, u32 tmem_offset, u32 pfn_offset, u32 len)
{
	struct tmem_op op;
	int rc = 0;

	op.cmd = tmem_cmd;
	op.pool_id = tmem_pool;
	op.u.gen.object = object;
	op.u.gen.index = index;
	op.u.gen.tmem_offset = tmem_offset;
	op.u.gen.pfn_offset = pfn_offset;
	op.u.gen.len = len;
	op.u.gen.cmfn = gmfn;
	rc = HYPERVISOR_tmem_op(&op);
	return rc;
}

int xen_tmem_new_pool(uint32_t tmem_cmd, uint64_t uuid_lo,
	uint64_t uuid_hi, uint32_t flags)
{
	struct tmem_op op;
	int rc = 0;

	op.cmd = tmem_cmd;
	op.u.new.uuid[0] = uuid_lo;
	op.u.new.uuid[1] = uuid_hi;
	op.u.new.flags = flags;
	rc = HYPERVISOR_tmem_op(&op);
	return rc;
}

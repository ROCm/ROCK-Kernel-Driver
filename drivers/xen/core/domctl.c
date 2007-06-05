/*
 * !!!  dirty hack alert  !!!
 *
 * Problem: old guests kernels don't have a "protocol" node
 *          in the frontend xenstore directory, so mixing
 *          32 and 64bit domains doesn't work.
 *
 * Upstream plans to solve this in the tools, by letting them
 * create a protocol node.  Which certainly makes sense.
 * But it isn't trivial and isn't done yet.  Too bad.
 *
 * So for the time being we use the get_address_size domctl
 * hypercall for a pretty good guess.  Not nice as the domctl
 * hypercall isn't supposed to be used by the kernel.  Because
 * we don't want to have dependencies between dom0 kernel and
 * xen kernel versions.  Now we have one.  Ouch.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/hypervisor.h>
#include <xen/blkif.h>

#include "domctl.h"

/* stuff copied from xen/interface/domctl.h, which we can't
 * include directly for the reasons outlined above .... */

#define XEN_DOMCTL_set_address_size 35
#define XEN_DOMCTL_get_address_size 36
typedef struct xen_domctl_address_size {
	uint32_t size;
} xen_domctl_address_size_t;

#define native_address_size (sizeof(unsigned long)*8)

/* v4: sles10 sp1: xen 3.0.4 + 32-on-64 patches */
struct xen_domctl_v4 {
	uint32_t cmd;
	uint32_t interface_version; /* XEN_DOMCTL_INTERFACE_VERSION */
	domid_t  domain;
	union {
		/* left out lots of other struct xen_domctl_foobar */
		struct xen_domctl_address_size       address_size;
		uint64_t                             dummy_align;
		uint8_t                              dummy_pad[128];
	} u;
};

/* v5: upstream: xen 3.0.5 */
typedef __attribute__((aligned(8))) uint64_t uint64_aligned_t;
struct xen_domctl_v5 {
	uint32_t cmd;
	uint32_t interface_version;
	domid_t  domain;
	union {
		struct xen_domctl_address_size       address_size;
		uint64_aligned_t                     dummy_align;
		uint8_t                              dummy_pad[128];
	} u;
};

/* The actual code comes here */

static int xen_guest_address_size_v4(int domid)
{
	struct xen_domctl_v4 domctl;
	int rc;

	memset(&domctl, 0, sizeof(domctl));
	domctl.cmd = XEN_DOMCTL_get_address_size;
	domctl.interface_version = 4;
	domctl.domain = domid;
	if (0 != (rc = _hypercall1(int, domctl, &domctl)))
		return rc;
	return domctl.u.address_size.size;
}

static int xen_guest_address_size_v5(int domid)
{
	struct xen_domctl_v5 domctl;
	int rc;

	memset(&domctl, 0, sizeof(domctl));
	domctl.cmd = XEN_DOMCTL_get_address_size;
	domctl.interface_version = 5;
	domctl.domain = domid;
	if (0 != (rc = _hypercall1(int, domctl, &domctl)))
		return rc;
	return domctl.u.address_size.size;
}

int xen_guest_address_size(int domid)
{
	int ret;

	ret = xen_guest_address_size_v4(domid);
	if (ret == 32 || ret == 64) {
		printk("%s: v4 domctl worked ok: %d\n", __FUNCTION__, ret);
		goto done;
	}

	ret = xen_guest_address_size_v5(domid);
	if (ret == 32 || ret == 64) {
		printk("%s: v5 domctl worked ok: %d\n", __FUNCTION__, ret);
		goto done;
	}

	ret = native_address_size;
	printk("%s: v4,v5 domctls failed, assuming native: %d\n",
	       __FUNCTION__, ret);

 done:
	return ret;
}
EXPORT_SYMBOL_GPL(xen_guest_address_size);

int xen_guest_blkif_protocol(int domid)
{
	int address_size;

	address_size = xen_guest_address_size(domid);
	printk(KERN_DEBUG "%s: domain %d: got address size %d\n",
	       __FUNCTION__, domid, address_size);
	if (address_size == native_address_size)
		return BLKIF_PROTOCOL_NATIVE;
	if (address_size == 32)
		return BLKIF_PROTOCOL_X86_32;
	if (address_size == 64)
		return BLKIF_PROTOCOL_X86_64;
	return BLKIF_PROTOCOL_NATIVE;
}
EXPORT_SYMBOL_GPL(xen_guest_blkif_protocol);

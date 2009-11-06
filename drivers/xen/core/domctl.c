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
#undef __XEN_PUBLIC_XEN_H__
#undef __XEN_TOOLS__
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/hypervisor.h>
#include <xen/blkif.h>

#include "domctl.h"

/* stuff copied from xen/interface/domctl.h, which we can't
 * include directly for the reasons outlined above .... */

typedef struct xen_domctl_address_size {
	uint32_t size;
} xen_domctl_address_size_t;

typedef __attribute__((aligned(8))) uint64_t uint64_aligned_t;

union xen_domctl {
	/* v4: sles10 sp1: xen 3.0.4 + 32-on-64 patches */
	struct {
		uint32_t cmd;
		uint32_t interface_version;
		domid_t  domain;
		union {
			/* left out lots of other struct xen_domctl_foobar */
			struct xen_domctl_address_size       address_size;
			uint64_t                             dummy_align;
			uint8_t                              dummy_pad[128];
		};
	} v4;

	/* v5: upstream: xen 3.1 */
	struct {
		uint32_t cmd;
		uint32_t interface_version;
		domid_t  domain;
		union {
			struct xen_domctl_address_size       address_size;
			uint64_aligned_t                     dummy_align;
			uint8_t                              dummy_pad[128];
		};
	} v5;
};

/* The actual code comes here */

static inline int hypervisor_domctl(void *domctl)
{
	return _hypercall1(int, domctl, domctl);
}

int xen_guest_address_size(int domid)
{
	union xen_domctl domctl;
	int low, ret;

#define guest_address_size(ver) do {					\
	memset(&domctl, 0, sizeof(domctl));				\
	domctl.v##ver.cmd = XEN_DOMCTL_get_address_size;		\
	domctl.v##ver.interface_version = low = ver;			\
	domctl.v##ver.domain = domid;					\
	ret = hypervisor_domctl(&domctl) ?: domctl.v##ver.address_size.size; \
	if (ret == 32 || ret == 64) {					\
		printk("v" #ver " domctl worked ok: dom%d is %d-bit\n",	\
		       domid, ret);					\
		return ret;						\
	}								\
} while (0)

	BUILD_BUG_ON(XEN_DOMCTL_INTERFACE_VERSION > 5);
	guest_address_size(5);
#if CONFIG_XEN_COMPAT < 0x030100
	guest_address_size(4);
#endif

	ret = BITS_PER_LONG;
	printk("v%d...5 domctls failed, assuming dom%d is native: %d\n",
	       low, domid, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(xen_guest_address_size);

int xen_guest_blkif_protocol(int domid)
{
	int address_size = xen_guest_address_size(domid);

	if (address_size == BITS_PER_LONG)
		return BLKIF_PROTOCOL_NATIVE;
	if (address_size == 32)
		return BLKIF_PROTOCOL_X86_32;
	if (address_size == 64)
		return BLKIF_PROTOCOL_X86_64;
	return BLKIF_PROTOCOL_NATIVE;
}
EXPORT_SYMBOL_GPL(xen_guest_blkif_protocol);

MODULE_LICENSE("GPL");

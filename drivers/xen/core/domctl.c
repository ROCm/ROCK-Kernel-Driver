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
#undef __XEN_PUBLIC_GRANT_TABLE_H__
#undef __XEN_TOOLS__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/percpu.h>
#include <asm/hypervisor.h>
#include <xen/blkif.h>

#include "domctl.h"

/* stuff copied from xen/interface/domctl.h, which we can't
 * include directly for the reasons outlined above .... */

typedef struct xen_domctl_address_size {
	uint32_t size;
} xen_domctl_address_size_t;

typedef __attribute__((aligned(8))) uint64_t uint64_aligned_t;

struct xenctl_cpumap_v4 {
	XEN_GUEST_HANDLE(uint8) bitmap;
	uint32_t nr_cpus;
};

struct xenctl_cpumap_v5 {
	union {
		XEN_GUEST_HANDLE(uint8) bitmap;
		uint64_aligned_t _align;
	};
	uint32_t nr_cpus;
};

struct xen_domctl_vcpuaffinity_v4 {
    uint32_t vcpu;
    struct xenctl_cpumap_v4 cpumap;
};

struct xen_domctl_vcpuaffinity_v5 {
    uint32_t vcpu;
    struct xenctl_cpumap_v5 cpumap;
};

union xen_domctl {
	/* v4: sles10 sp1: xen 3.0.4 + 32-on-64 patches */
	struct {
		uint32_t cmd;
		uint32_t interface_version;
		domid_t  domain;
		union {
			/* left out lots of other struct xen_domctl_foobar */
			struct xen_domctl_address_size       address_size;
			struct xen_domctl_vcpuaffinity_v4    vcpu_affinity;
			uint64_t                             dummy_align;
			uint8_t                              dummy_pad[128];
		};
	} v4;

	/* v5: upstream: xen 3.1, v6: upstream: xen 4.0 */
	struct {
		uint32_t cmd;
		uint32_t interface_version;
		domid_t  domain;
		union {
			struct xen_domctl_address_size       address_size;
			struct xen_domctl_vcpuaffinity_v5    vcpu_affinity;
			uint64_aligned_t                     dummy_align;
			uint8_t                              dummy_pad[128];
		};
	} v5, v6;
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

	BUILD_BUG_ON(XEN_DOMCTL_INTERFACE_VERSION > 6);
	guest_address_size(6);
#if CONFIG_XEN_COMPAT < 0x040000
	guest_address_size(5);
#endif
#if CONFIG_XEN_COMPAT < 0x030100
	guest_address_size(4);
#endif

	ret = BITS_PER_LONG;
	printk("v%d...6 domctls failed, assuming dom%d is native: %d\n",
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

#ifdef CONFIG_X86

#define vcpuaffinity(what, ver) ({					\
	memset(&domctl, 0, sizeof(domctl));				\
	domctl.v##ver.cmd = XEN_DOMCTL_##what##vcpuaffinity;		\
	domctl.v##ver.interface_version = ver;				\
	/* domctl.v##ver.domain = 0; */					\
	domctl.v##ver.vcpu_affinity.vcpu = smp_processor_id();		\
	domctl.v##ver.vcpu_affinity.cpumap.nr_cpus = nr;		\
	set_xen_guest_handle(domctl.v##ver.vcpu_affinity.cpumap.bitmap, \
			     mask);					\
	hypervisor_domctl(&domctl);					\
})

static inline int get_vcpuaffinity(unsigned int nr, void *mask)
{
	union xen_domctl domctl;
	int rc;

	BUILD_BUG_ON(XEN_DOMCTL_INTERFACE_VERSION > 6);
	rc = vcpuaffinity(get, 6);
#if CONFIG_XEN_COMPAT < 0x040000
	if (rc)
		rc = vcpuaffinity(get, 5);
#endif
#if CONFIG_XEN_COMPAT < 0x030100
	if (rc)
		rc = vcpuaffinity(get, 4);
#endif
	return rc;
}

static inline int set_vcpuaffinity(unsigned int nr, void *mask)
{
	union xen_domctl domctl;
	int rc;

	BUILD_BUG_ON(XEN_DOMCTL_INTERFACE_VERSION > 6);
	rc = vcpuaffinity(set, 6);
#if CONFIG_XEN_COMPAT < 0x040000
	if (rc)
		rc = vcpuaffinity(set, 5);
#endif
#if CONFIG_XEN_COMPAT < 0x030100
	if (rc)
		rc = vcpuaffinity(set, 4);
#endif
	return rc;
}

static DEFINE_PER_CPU(void *, saved_pcpu_affinity);

#define BITS_PER_PAGE (PAGE_SIZE * BITS_PER_LONG / sizeof(long))

int xen_set_physical_cpu_affinity(int pcpu)
{
	int rc;

	if (!is_initial_xendomain())
		return -EPERM;

	if (pcpu >= 0) {
		void *oldmap;

		if (pcpu > BITS_PER_PAGE)
			return -ERANGE;

		if (percpu_read(saved_pcpu_affinity))
			return -EBUSY;

		oldmap = (void *)get_zeroed_page(GFP_KERNEL);
		if (!oldmap)
			return -ENOMEM;

		rc = get_vcpuaffinity(BITS_PER_PAGE, oldmap);
		if (!rc) {
			void *newmap = kzalloc(BITS_TO_LONGS(pcpu + 1)
					       * sizeof(long), GFP_KERNEL);

			if (newmap) {
				__set_bit(pcpu, newmap);
				rc = set_vcpuaffinity(pcpu + 1, newmap);
				kfree(newmap);
			} else
				rc = -ENOMEM;
		}

		if (!rc)
			percpu_write(saved_pcpu_affinity, oldmap);
		else
			free_page((unsigned long)oldmap);
	} else {
		if (!percpu_read(saved_pcpu_affinity))
			return 0;
		rc = set_vcpuaffinity(BITS_PER_PAGE,
				      percpu_read(saved_pcpu_affinity));
		free_page((unsigned long)percpu_read(saved_pcpu_affinity));
		percpu_write(saved_pcpu_affinity, NULL);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(xen_set_physical_cpu_affinity);

#endif /* CONFIG_X86 */

MODULE_LICENSE("GPL");

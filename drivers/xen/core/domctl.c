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
#include <linux/slab.h>
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
	/* v4: sle10 sp1: xen 3.0.4 + 32-on-64 patches */
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

	/*
	 * v5: upstream: xen 3.1
	 * v6: upstream: xen 4.0
	 * v7: sle11 sp1: xen 4.0 + cpupools patches
	 */
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
	} v5, v6, v7;
};

struct xen_sysctl_physinfo_v6 {
	uint32_t threads_per_core;
	uint32_t cores_per_socket;
	uint32_t nr_cpus;
	uint32_t nr_nodes;
	uint32_t cpu_khz;
	uint64_aligned_t total_pages;
	uint64_aligned_t free_pages;
	uint64_aligned_t scrub_pages;
	uint32_t hw_cap[8];
	uint32_t max_cpu_id;
	union {
		XEN_GUEST_HANDLE(uint32) cpu_to_node;
		uint64_aligned_t _ctn_align;
	};
	uint32_t capabilities;
};

struct xen_sysctl_physinfo_v7 {
	uint32_t threads_per_core;
	uint32_t cores_per_socket;
	uint32_t nr_cpus;
	uint32_t max_node_id;
	uint32_t cpu_khz;
	uint64_aligned_t total_pages;
	uint64_aligned_t free_pages;
	uint64_aligned_t scrub_pages;
	uint32_t hw_cap[8];
	uint32_t max_cpu_id;
	union {
		XEN_GUEST_HANDLE(uint32) cpu_to_node;
		uint64_aligned_t _ctn_align;
	};
	uint32_t capabilities;
};

#define XEN_SYSCTL_pm_op_get_cputopo 0x20
struct xen_get_cputopo_v6 {
	uint32_t max_cpus;
	union {
		XEN_GUEST_HANDLE(uint32) cpu_to_core;
		uint64_aligned_t _ctc_align;
	};
	union {
		XEN_GUEST_HANDLE(uint32) cpu_to_socket;
		uint64_aligned_t _cts_align;
	};
	uint32_t nr_cpus;
};

struct xen_sysctl_pm_op_v6 {
	uint32_t cmd;
	uint32_t cpuid;
	union {
		struct xen_get_cputopo_v6 get_topo;
	};
};
#define xen_sysctl_pm_op_v7 xen_sysctl_pm_op_v6

struct xen_sysctl_topologyinfo_v8 {
	uint32_t max_cpu_index;
	union {
		XEN_GUEST_HANDLE(uint32) cpu_to_core;
		uint64_aligned_t _ctc_align;
	};
	union {
		XEN_GUEST_HANDLE(uint32) cpu_to_socket;
		uint64_aligned_t _cts_align;
	};
	union {
		XEN_GUEST_HANDLE(uint32) cpu_to_node;
		uint64_aligned_t _ctn_align;
	};
};

union xen_sysctl {
	/* v6: Xen 3.4.x */
	struct {
		uint32_t cmd;
		uint32_t interface_version;
		union {
			struct xen_sysctl_physinfo_v6 physinfo;
			struct xen_sysctl_pm_op_v6 pm_op;
		};
	} v6;
	/* v7: Xen 4.0.x */
	struct {
		uint32_t cmd;
		uint32_t interface_version;
		union {
			struct xen_sysctl_physinfo_v7 physinfo;
			struct xen_sysctl_pm_op_v7 pm_op;
		};
	} v7;
	/* v8: Xen 4.1+ */
	struct {
		uint32_t cmd;
		uint32_t interface_version;
		union {
			struct xen_sysctl_topologyinfo_v8 topologyinfo;
		};
	} v8;
};

/* The actual code comes here */

static inline int hypervisor_domctl(void *domctl)
{
	return _hypercall1(int, domctl, domctl);
}

static inline int hypervisor_sysctl(void *sysctl)
{
	return _hypercall1(int, sysctl, sysctl);
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
		pr_info("v" #ver " domctl worked ok: dom%d is %d-bit\n",\
			domid, ret);					\
		return ret;						\
	}								\
} while (0)

	BUILD_BUG_ON(XEN_DOMCTL_INTERFACE_VERSION > 7);
	guest_address_size(7);
#if CONFIG_XEN_COMPAT < 0x040100
	guest_address_size(6);
#endif
#if CONFIG_XEN_COMPAT < 0x040000
	guest_address_size(5);
#endif
#if CONFIG_XEN_COMPAT < 0x030100
	guest_address_size(4);
#endif

	ret = BITS_PER_LONG;
	pr_warn("v%d...%d domctls failed, assuming dom%d is native: %d\n",
		low, XEN_DOMCTL_INTERFACE_VERSION, domid, ret);

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

	BUILD_BUG_ON(XEN_DOMCTL_INTERFACE_VERSION > 7);
	rc = vcpuaffinity(get, 7);
#if CONFIG_XEN_COMPAT < 0x040100
	if (rc)
		rc = vcpuaffinity(get, 6);
#endif
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

	BUILD_BUG_ON(XEN_DOMCTL_INTERFACE_VERSION > 7);
	rc = vcpuaffinity(set, 7);
#if CONFIG_XEN_COMPAT < 0x040100
	if (rc)
		rc = vcpuaffinity(set, 6);
#endif
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

int xen_get_topology_info(unsigned int cpu, u32 *core, u32 *sock, u32 *node)
{
	union xen_sysctl sysctl;
	uint32_t *cores = NULL, *socks = NULL, *nodes = NULL;
	unsigned int nr;
	int rc;

	if (core)
		cores = kmalloc((cpu + 1) * sizeof(*cores), GFP_KERNEL);
	if (sock)
		socks = kmalloc((cpu + 1) * sizeof(*socks), GFP_KERNEL);
	if (node)
		nodes = kmalloc((cpu + 1) * sizeof(*nodes), GFP_KERNEL);
	if ((core && !cores) || (sock && !socks) || (node && !nodes)) {
		kfree(cores);
		kfree(socks);
		kfree(nodes);
		return -ENOMEM;
	}

#define topologyinfo(ver) do {						\
	memset(&sysctl, 0, sizeof(sysctl));				\
	sysctl.v##ver.cmd = XEN_SYSCTL_topologyinfo;			\
	sysctl.v##ver.interface_version = ver;				\
	sysctl.v##ver.topologyinfo.max_cpu_index = cpu;			\
	set_xen_guest_handle(sysctl.v##ver.topologyinfo.cpu_to_core,	\
			     cores);					\
	set_xen_guest_handle(sysctl.v##ver.topologyinfo.cpu_to_socket,	\
			     socks);					\
	set_xen_guest_handle(sysctl.v##ver.topologyinfo.cpu_to_node,	\
			     nodes);					\
	rc = hypervisor_sysctl(&sysctl);				\
	nr = sysctl.v##ver.topologyinfo.max_cpu_index + 1;		\
} while (0)

	BUILD_BUG_ON(XEN_SYSCTL_INTERFACE_VERSION > 8);
	topologyinfo(8);

#if CONFIG_XEN_COMPAT < 0x040100
#define pm_op_cputopo(ver) do {						\
	memset(&sysctl, 0, sizeof(sysctl));				\
	sysctl.v##ver.cmd = XEN_SYSCTL_pm_op;				\
	sysctl.v##ver.interface_version = ver;				\
	sysctl.v##ver.pm_op.cmd = XEN_SYSCTL_pm_op_get_cputopo;		\
	sysctl.v##ver.pm_op.cpuid = 0;					\
	sysctl.v##ver.pm_op.get_topo.max_cpus = cpu + 1;		\
	set_xen_guest_handle(sysctl.v##ver.pm_op.get_topo.cpu_to_core,	\
			     cores);					\
	set_xen_guest_handle(sysctl.v##ver.pm_op.get_topo.cpu_to_socket,\
			     socks);					\
	rc = hypervisor_sysctl(&sysctl);				\
	memset(&sysctl, 0, sizeof(sysctl));				\
	sysctl.v##ver.cmd = XEN_SYSCTL_physinfo;			\
	sysctl.v##ver.interface_version = ver;				\
	sysctl.v##ver.physinfo.max_cpu_id = cpu;			\
	set_xen_guest_handle(sysctl.v##ver.physinfo.cpu_to_node, nodes);\
	rc = hypervisor_sysctl(&sysctl) ?: rc;				\
	nr = sysctl.v##ver.physinfo.max_cpu_id + 1;			\
} while (0)

	if (rc)
		pm_op_cputopo(7);
#endif
#if CONFIG_XEN_COMPAT < 0x040000
	if (rc)
		pm_op_cputopo(6);
#endif

	if (!rc && cpu >= nr)
		rc = -EDOM;

	if (!rc && core && (*core = cores[cpu]) == INVALID_TOPOLOGY_ID)
		rc = -ENOENT;
	kfree(cores);

	if (!rc && sock && (*sock = socks[cpu]) == INVALID_TOPOLOGY_ID)
		rc = -ENOENT;
	kfree(socks);

	if (!rc && node && (*node = nodes[cpu]) == INVALID_TOPOLOGY_ID)
		rc = -ENOENT;
	kfree(nodes);

	return rc;
}
EXPORT_SYMBOL_GPL(xen_get_topology_info);

#include <xen/pcpu.h>
#include <asm/msr.h>

int rdmsr_safe_on_pcpu(unsigned int pcpu, u32 msr_no, u32 *l, u32 *h)
{
	int err = xen_set_physical_cpu_affinity(pcpu);

	switch (err) {
	case 0:
		err = rdmsr_safe(msr_no, l, h);
		WARN_ON_ONCE(xen_set_physical_cpu_affinity(-1));
		break;
	case -EINVAL:
		/* Fall back in case this is due to dom0_vcpus_pinned. */
		err = rdmsr_safe_on_cpu(pcpu, msr_no, l, h) ?: 1;
		break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(rdmsr_safe_on_pcpu);

int wrmsr_safe_on_pcpu(unsigned int pcpu, u32 msr_no, u32 l, u32 h)
{
	int err = xen_set_physical_cpu_affinity(pcpu);

	switch (err) {
	case 0:
		err = wrmsr_safe(msr_no, l, h);
		WARN_ON_ONCE(xen_set_physical_cpu_affinity(-1));
		break;
	case -EINVAL:
		/* Fall back in case this is due to dom0_vcpus_pinned. */
		err = wrmsr_safe_on_cpu(pcpu, msr_no, l, h) ?: 1;
		break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(wrmsr_safe_on_pcpu);

int rdmsr_safe_regs_on_pcpu(unsigned int pcpu, u32 *regs)
{
	int err = xen_set_physical_cpu_affinity(pcpu);

	switch (err) {
	case 0:
		err = rdmsr_safe_regs(regs);
		WARN_ON_ONCE(xen_set_physical_cpu_affinity(-1));
		break;
	case -EINVAL:
		/* Fall back in case this is due to dom0_vcpus_pinned. */
		err = rdmsr_safe_regs_on_cpu(pcpu, regs) ?: 1;
		break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(rdmsr_safe_regs_on_pcpu);

int wrmsr_safe_regs_on_pcpu(unsigned int pcpu, u32 *regs)
{
	int err = xen_set_physical_cpu_affinity(pcpu);

	switch (err) {
	case 0:
		err = wrmsr_safe_regs(regs);
		WARN_ON_ONCE(xen_set_physical_cpu_affinity(-1));
		break;
	case -EINVAL:
		/* Fall back in case this is due to dom0_vcpus_pinned. */
		err = wrmsr_safe_regs_on_cpu(pcpu, regs) ?: 1;
		break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(wrmsr_safe_regs_on_pcpu);

#endif /* CONFIG_X86 */

MODULE_LICENSE("GPL");

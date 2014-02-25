/*
 * drivers/xen/core/machine_kexec.c 
 * handle transition of Linux booting another kernel
 */

#include <linux/kexec.h>
#include <linux/slab.h>
#include <xen/interface/kexec.h>
#include <xen/interface/platform.h>
#include <linux/reboot.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <xen/pcpu.h>

extern void machine_kexec_setup_load_arg(xen_kexec_image_t *xki, 
					 struct kimage *image);
extern int machine_kexec_setup_resources(struct resource *hypervisor,
					 struct resource *phys_cpus,
					 int nr_phys_cpus);
extern int machine_kexec_setup_resource(struct resource *hypervisor,
					struct resource *phys_cpu);
extern void machine_kexec_register_resources(struct resource *res);

static unsigned int xen_nr_phys_cpus, xen_max_nr_phys_cpus;
static struct resource xen_hypervisor_res;
static struct resource *xen_phys_cpus;
static struct xen_phys_cpu_entry {
	struct xen_phys_cpu_entry *next;
	struct resource res;
} *xen_phys_cpu_list;

size_t vmcoreinfo_size_xen;
unsigned long paddr_vmcoreinfo_xen;

static int fill_crash_res(struct resource *res, unsigned int cpu)
{
	xen_kexec_range_t range = {
		.range = KEXEC_RANGE_MA_CPU,
		.nr = cpu
	};
	int rc = HYPERVISOR_kexec_op(KEXEC_CMD_kexec_get_range, &range);

	if (!rc && !range.size)
		rc = -ENODEV;
	if (!rc) {
		res->name = "Crash note";
		res->start = range.start;
		res->end = range.start + range.size - 1;
		res->flags = IORESOURCE_BUSY | IORESOURCE_MEM;
	}

	return rc;
}

static struct resource *find_crash_res(const struct resource *r,
				       unsigned int *idx)
{
	unsigned int i;
	struct xen_phys_cpu_entry *ent;

	for (i = 0; i < xen_max_nr_phys_cpus; ++i) {
		struct resource *res = xen_phys_cpus + i;

		if (res->parent && res->start == r->start
		    && res->end == r->end) {
			if (idx)
				*idx = i;
			return res;
		}
	}

	for (ent = xen_phys_cpu_list; ent; ent = ent->next, ++i)
		if (ent->res.parent && ent->res.start == r->start
		    && ent->res.end == r->end) {
			if (idx)
				*idx = i;
			return &ent->res;
		}

	return NULL;
}

static int kexec_cpu_callback(struct notifier_block *nfb,
			      unsigned long action, void *hcpu)
{
	unsigned int i, cpu = (unsigned long)hcpu;
	struct xen_phys_cpu_entry *ent;
	struct resource *res = NULL, r;

	if (xen_nr_phys_cpus < xen_max_nr_phys_cpus)
		xen_nr_phys_cpus = xen_max_nr_phys_cpus;
	switch (action) {
	case CPU_ONLINE:
		for (i = 0; i < xen_max_nr_phys_cpus; ++i)
			if (!xen_phys_cpus[i].parent) {
				res = xen_phys_cpus + i;
				break;
			}
		if (!res)
			for (ent = xen_phys_cpu_list; ent; ent = ent->next)
				if (!ent->res.parent) {
					res = &ent->res;
					break;
				}
		if (!res) {
			ent = kmalloc(sizeof(*ent), GFP_KERNEL);
			res = ent ? &ent->res : NULL;
		} else
			ent = NULL;
		if (res && !fill_crash_res(res, cpu)
		    && !machine_kexec_setup_resource(&xen_hypervisor_res,
						     res)) {
			if (ent) {
				ent->next = xen_phys_cpu_list;
				xen_phys_cpu_list = ent;
				++xen_nr_phys_cpus;
			}
		} else {
			pr_warn("Could not set up crash note for pCPU#%u\n",
				cpu);
			kfree(ent);
		}
		break;

	case CPU_DEAD:
		if (!fill_crash_res(&r, cpu))
			res = find_crash_res(&r, NULL);
		if (!res) {
			unsigned long *map;
			xen_platform_op_t op;

			map = kcalloc(BITS_TO_LONGS(xen_nr_phys_cpus),
				      sizeof(long), GFP_KERNEL);
			if (!map)
				break;

			op.cmd = XENPF_get_cpuinfo;
			op.u.pcpu_info.xen_cpuid = 0;
			if (HYPERVISOR_platform_op(&op) == 0)
				i = op.u.pcpu_info.max_present + 1;
			else
				i = xen_nr_phys_cpus;

			for (cpu = 0; cpu < i; ++cpu) {
				unsigned int idx;

				if (fill_crash_res(&r, cpu))
					continue;
				if (find_crash_res(&r, &idx)) {
					BUG_ON(idx >= xen_nr_phys_cpus);
					__set_bit(idx, map);
				}
			}

			for (i = 0; i < xen_max_nr_phys_cpus; ++i)
				if (xen_phys_cpus[i].parent && !test_bit(i, map)) {
					res = xen_phys_cpus + i;
					break;
				}
			for (ent = xen_phys_cpu_list; !res && ent;
			     ent = ent->next, ++i)
				if (ent->res.parent && !test_bit(i, map))
					res = &ent->res;
			kfree(map);
		}
		if (res)
			release_resource(res);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block kexec_cpu_notifier = {
	.notifier_call = kexec_cpu_callback
};

void __init xen_machine_kexec_setup_resources(void)
{
	xen_kexec_range_t range;
	xen_platform_op_t op;
	unsigned int k = 0, nr = 0;
	int rc;

	if (strstr(boot_command_line, "crashkernel="))
		pr_warning("Ignoring crashkernel command line, "
			   "parameter will be supplied by xen\n");

	if (!is_initial_xendomain())
		return;

	/* fill in crashk_res if range is reserved by hypervisor */
	memset(&range, 0, sizeof(range));
	range.range = KEXEC_RANGE_MA_CRASH;

	if (HYPERVISOR_kexec_op(KEXEC_CMD_kexec_get_range, &range)
	    || !range.size)
		return;

	crashk_res.start = range.start;
	crashk_res.end = range.start + range.size - 1;

	/* determine maximum number of physical cpus */
	op.cmd = XENPF_get_cpuinfo;
	op.u.pcpu_info.xen_cpuid = 0;
	if (HYPERVISOR_platform_op(&op) == 0)
		k = op.u.pcpu_info.max_present + 1;
#if CONFIG_XEN_COMPAT < 0x040000
	else while (1) {
		memset(&range, 0, sizeof(range));
		range.range = KEXEC_RANGE_MA_CPU;
		range.nr = k;

		if(HYPERVISOR_kexec_op(KEXEC_CMD_kexec_get_range, &range))
			break;

		k++;
	}
#endif

	if (k == 0)
		return;

	xen_max_nr_phys_cpus = k;

	/* allocate xen_phys_cpus */

	xen_phys_cpus = alloc_bootmem(k * sizeof(struct resource));

	/* fill in xen_phys_cpus with per-cpu crash note information */

	for (k = 0; k < xen_max_nr_phys_cpus; k++)
		if (!fill_crash_res(xen_phys_cpus + nr, k))
			++nr;

	if (nr == 0)
		goto free;

	/* fill in xen_hypervisor_res with hypervisor machine address range */

	memset(&range, 0, sizeof(range));
	range.range = KEXEC_RANGE_MA_XEN;

	if (HYPERVISOR_kexec_op(KEXEC_CMD_kexec_get_range, &range))
		goto free;

	xen_hypervisor_res.name = "Hypervisor code and data";
	xen_hypervisor_res.start = range.start;
	xen_hypervisor_res.end = range.start + range.size - 1;
	xen_hypervisor_res.flags = IORESOURCE_BUSY | IORESOURCE_MEM;

	/* get physical address of vmcoreinfo */
	memset(&range, 0, sizeof(range));
	range.range = KEXEC_RANGE_MA_VMCOREINFO;

	rc = HYPERVISOR_kexec_op(KEXEC_CMD_kexec_get_range, &range);

	if (rc == 0) {
		/* Hypercall succeeded */
		vmcoreinfo_size_xen = range.size;
		paddr_vmcoreinfo_xen = range.start;

	} else {
		/* Hypercall failed.
		 * Indicate not to create sysfs file by resetting globals
		 */
		vmcoreinfo_size_xen = 0;
		paddr_vmcoreinfo_xen = 0;
		
#if CONFIG_XEN_COMPAT < 0x030300
		/* The KEXEC_CMD_kexec_get_range hypercall did not implement
		 * KEXEC_RANGE_MA_VMCOREINFO until Xen 3.3.
		 * Do not bail out if it fails for this reason.
		 */
		if (rc != -EINVAL)
#endif
			goto free;
	}

	if (machine_kexec_setup_resources(&xen_hypervisor_res, xen_phys_cpus,
					  nr)) {
		/*
		 * It's too cumbersome to properly free xen_phys_cpus here.
		 * Failure at this stage is unexpected and the amount of
		 * memory is small therefore we tolerate the potential leak.
		 */
		goto err;
	}

	xen_nr_phys_cpus = nr;
	rc = register_pcpu_notifier(&kexec_cpu_notifier);
	if (rc)
		pr_warn("kexec: pCPU notifier registration failed (%d)\n", rc);

	return;

 free:
	free_bootmem(__pa(xen_phys_cpus),
		     xen_max_nr_phys_cpus * sizeof(*xen_phys_cpus));
 err:
	xen_nr_phys_cpus = 0;
}

#ifndef CONFIG_X86
void __init xen_machine_kexec_register_resources(struct resource *res)
{
	int k;
	struct resource *r;

	request_resource(res, &xen_hypervisor_res);
	for (k = 0; k < xen_nr_phys_cpus; k++) {
		r = xen_phys_cpus + k;
		if (r->parent == NULL) /* out of xen_hypervisor_res range */
			request_resource(res, r);
	} 
	machine_kexec_register_resources(res);
}
#endif

static void setup_load_arg(xen_kexec_image_t *xki, struct kimage *image)
{
	machine_kexec_setup_load_arg(xki, image);

	xki->indirection_page = image->head;
	xki->start_address = image->start;
}

/*
 * Load the image into xen so xen can kdump itself
 * This might have been done in prepare, but prepare
 * is currently called too early. It might make sense
 * to move prepare, but for now, just add an extra hook.
 */
int xen_machine_kexec_load(struct kimage *image)
{
	xen_kexec_load_v1_t xkl;

	memset(&xkl, 0, sizeof(xkl));
	xkl.type = image->type;
	setup_load_arg(&xkl.image, image);
	return HYPERVISOR_kexec_op(KEXEC_CMD_kexec_load_v1, &xkl);
}

/*
 * Unload the image that was stored by machine_kexec_load()
 * This might have been done in machine_kexec_cleanup() but it
 * is called too late, and its possible xen could try and kdump
 * using resources that have been freed.
 */
void xen_machine_kexec_unload(struct kimage *image)
{
	xen_kexec_load_v1_t xkl;

	memset(&xkl, 0, sizeof(xkl));
	xkl.type = image->type;
	WARN_ON(HYPERVISOR_kexec_op(KEXEC_CMD_kexec_unload_v1, &xkl));
}

/*
 * Do not allocate memory (or fail in any way) in machine_kexec().
 * We are past the point of no return, committed to rebooting now.
 *
 * This has the hypervisor move to the prefered reboot CPU, 
 * stop all CPUs and kexec. That is it combines machine_shutdown()
 * and machine_kexec() in Linux kexec terms.
 */
void __noreturn machine_kexec(struct kimage *image)
{
	xen_kexec_exec_t xke;

	memset(&xke, 0, sizeof(xke));
	xke.type = image->type;
	VOID(HYPERVISOR_kexec_op(KEXEC_CMD_kexec, &xke));
	panic("KEXEC_CMD_kexec hypercall should not return\n");
}

#ifdef CONFIG_X86
unsigned long paddr_vmcoreinfo_note(void)
{
	return virt_to_machine(&vmcoreinfo_note);
}
#endif

void machine_shutdown(void)
{
	/* do nothing */
}

void machine_crash_shutdown(struct pt_regs *regs)
{
	/* The kernel is broken so disable interrupts */
	local_irq_disable();
}

/*
 * Local variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */

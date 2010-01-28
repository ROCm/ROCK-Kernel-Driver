#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <xen/interface/xen.h>
#include <xen/evtchn.h>
#include <xen/interface/vcpu.h>
#include <asm/hypercall.h>
#include <asm/mce.h>

static xen_mc_logical_cpu_t *g_physinfo;
static unsigned int ncpus;

static int convert_log(struct mc_info *mi)
{
	struct mcinfo_common *mic = NULL;
	struct mcinfo_global *mc_global;
	struct mcinfo_bank *mc_bank;
	struct mce m;
	unsigned int i;
	bool found = false;

	x86_mcinfo_lookup(mic, mi, MC_TYPE_GLOBAL);
	if (mic == NULL)
	{
		printk(KERN_ERR "DOM0_MCE_LOG: global data is NULL\n");
		return -1;
	}

	mce_setup(&m);
	mc_global = (struct mcinfo_global*)mic;
	m.mcgstatus = mc_global->mc_gstatus;
	m.apicid = mc_global->mc_apicid;

	for (i = 0; i < ncpus; i++)
		if (g_physinfo[i].mc_apicid == m.apicid) {
			found = true;
			break;
		}
	WARN_ON_ONCE(!found);
	m.socketid = g_physinfo[i].mc_chipid;
	m.cpu = m.extcpu = g_physinfo[i].mc_cpunr;
	m.cpuvendor = (__u8)g_physinfo[i].mc_vendor;

	x86_mcinfo_lookup(mic, mi, MC_TYPE_BANK);
	do
	{
		if (mic == NULL || mic->size == 0)
			break;
		if (mic->type == MC_TYPE_BANK)
		{
			mc_bank = (struct mcinfo_bank*)mic;
			m.misc = mc_bank->mc_misc;
			m.status = mc_bank->mc_status;
			m.addr = mc_bank->mc_addr;
			m.tsc = mc_bank->mc_tsc;
			m.bank = mc_bank->mc_bank;
			printk(KERN_DEBUG "[CPU%d, BANK%d, addr %llx, state %llx]\n", 
						m.bank, m.cpu, m.addr, m.status);
			/*log this record*/
			mce_log(&m);
		}
		mic = x86_mcinfo_next(mic);
	}while (1);

	return 0;
}

static struct mc_info *g_mi;

/*dom0 mce virq handler, logging physical mce error info*/

static irqreturn_t mce_dom0_interrupt(int irq, void *dev_id)
{
	xen_mc_t mc_op;
	int result = 0;

	printk(KERN_DEBUG "MCE_DOM0_LOG: enter dom0 mce vIRQ handler\n");
	mc_op.cmd = XEN_MC_fetch;
	mc_op.interface_version = XEN_MCA_INTERFACE_VERSION;
	set_xen_guest_handle(mc_op.u.mc_fetch.data, g_mi);
urgent:
	mc_op.u.mc_fetch.flags = XEN_MC_URGENT;
	result = HYPERVISOR_mca(&mc_op);
	if (result || mc_op.u.mc_fetch.flags & XEN_MC_NODATA ||
			mc_op.u.mc_fetch.flags & XEN_MC_FETCHFAILED)
	{
		printk(KERN_DEBUG "MCE_DOM0_LOG: No more urgent data\n");
		goto nonurgent;
	}
	else
	{
		result = convert_log(g_mi);
		if (result) {
			printk(KERN_ERR "MCE_DOM0_LOG: Log conversion failed\n");
			goto end;
		}
		/* After fetching the telem from DOM0, we need to dec the telem's
		 * refcnt and release the entry. The telem is reserved and inc
		 * refcnt when filling the telem.
		 */
		mc_op.u.mc_fetch.flags = XEN_MC_URGENT | XEN_MC_ACK;
		result = HYPERVISOR_mca(&mc_op);

		goto urgent;
	}
nonurgent:
	mc_op.u.mc_fetch.flags = XEN_MC_NONURGENT;
	result = HYPERVISOR_mca(&mc_op);
	if (result || mc_op.u.mc_fetch.flags & XEN_MC_NODATA ||
			mc_op.u.mc_fetch.flags & XEN_MC_FETCHFAILED)
	{
		printk(KERN_DEBUG "MCE_DOM0_LOG: No more nonurgent data\n");
		goto end;
	}
	else
	{
		result = convert_log(g_mi);
		if (result) {
			printk(KERN_ERR "MCE_DOM0_LOG: Log conversion failed\n");
			goto end;
		}
		/* After fetching the telem from DOM0, we need to dec the telem's
		 * refcnt and release the entry. The telem is reserved and inc
		 * refcnt when filling the telem.
		 */
		mc_op.u.mc_fetch.flags = XEN_MC_NONURGENT | XEN_MC_ACK;
		result = HYPERVISOR_mca(&mc_op);

		goto nonurgent;
	}
end:
	return IRQ_HANDLED;
}

int __init bind_virq_for_mce(void)
{
	int ret;
	xen_mc_t mc_op;

	g_mi = kmalloc(sizeof(*g_mi), GFP_KERNEL);
	if (!g_mi)
		return -ENOMEM;

	/* fetch physical CPU count */
	mc_op.cmd = XEN_MC_physcpuinfo;
	mc_op.interface_version = XEN_MCA_INTERFACE_VERSION;
	set_xen_guest_handle(mc_op.u.mc_physcpuinfo.info, NULL);
	ret = HYPERVISOR_mca(&mc_op);
	if (ret) {
		printk(KERN_ERR "MCE: Failed to get physical CPU count\n");
		kfree(g_mi);
		return ret;
	}

	/* fetch CPU physical info for later reference */
	ncpus = mc_op.u.mc_physcpuinfo.ncpus;
	g_physinfo = kmalloc(sizeof(*g_physinfo) * ncpus, GFP_KERNEL);
	if (!g_physinfo) {
		kfree(g_mi);
		return -ENOMEM;
	}
	set_xen_guest_handle(mc_op.u.mc_physcpuinfo.info, g_physinfo);
	ret = HYPERVISOR_mca(&mc_op);
	if (ret) {
		printk(KERN_ERR "MCE: Failed to get physical CPUs' info\n");
		kfree(g_mi);
		kfree(g_physinfo);
		return ret;
	}

	ret  = bind_virq_to_irqhandler(VIRQ_MCA, 0, 
		mce_dom0_interrupt, 0, "mce", NULL);

	if (ret < 0) {
		printk(KERN_ERR "MCE: Failed to bind vIRQ for Dom0\n");
		kfree(g_mi);
		kfree(g_physinfo);
		return ret;
	}

	/* Log the machine checks left over from the previous reset. */
	mce_dom0_interrupt(VIRQ_MCA, NULL);

	return 0;
}


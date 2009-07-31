#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <xen/interface/xen.h>
#include <xen/evtchn.h>
#include <xen/interface/vcpu.h>
#include <asm/hypercall.h>
#include <asm/mce.h>

static int convert_log(struct mc_info *mi)
{
	struct mcinfo_common *mic = NULL;
	struct mcinfo_global *mc_global;
	struct mcinfo_bank *mc_bank;
	struct mce m;

	x86_mcinfo_lookup(mic, mi, MC_TYPE_GLOBAL);
	if (mic == NULL)
	{
		printk(KERN_ERR "DOM0_MCE_LOG: global data is NULL\n");
		return -1;
	}

	mc_global = (struct mcinfo_global*)mic;
	m.mcgstatus = mc_global->mc_gstatus;
	m.cpu = mc_global->mc_coreid;/*for test*/
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
/*
 * The res1 field no longer exists, and I don't think adding a new field to
 * the end of struct mce would be forward compatible.
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
			m.res1 = mc_bank->mc_ctrl2;
#endif
 */
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

void bind_virq_for_mce(void)
{
	int ret;

	ret  = bind_virq_to_irqhandler(VIRQ_MCA, 0, 
		mce_dom0_interrupt, 0, "mce", NULL);

	g_mi = kmalloc(sizeof(struct mc_info), GFP_KERNEL);
	if (ret < 0)
		printk(KERN_ERR "MCE_DOM0_LOG: bind_virq for DOM0 failed\n");
}


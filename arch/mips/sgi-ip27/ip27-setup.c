/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI IP27 specific setup.
 *
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silcon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/smp.h>

#include <asm/io.h>
#include <asm/sn/types.h>
#include <asm/sn/sn0/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/sn0/hubio.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/ioc3.h>
#include <asm/time.h>
#include <asm/mipsregs.h>
#include <asm/sn/arch.h>
#include <asm/sn/sn_private.h>
#include <asm/pci/bridge.h>
#include <asm/paccess.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/traps.h>

/* Check against user dumbness.  */
#ifdef CONFIG_VT
#error CONFIG_VT not allowed for IP27.
#endif

#undef DEBUG_SETUP
#ifdef DEBUG_SETUP
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

extern void ip27_be_init(void) __init;

/*
 * get_nasid() returns the physical node id number of the caller.
 */
nasid_t
get_nasid(void)
{
	return (nasid_t)((LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_NODEID_MASK)
	                 >> NSRI_NODEID_SHFT);
}

/* Extracted from the IOC3 meta driver.  FIXME.  */
static inline void ioc3_sio_init(void)
{
	struct ioc3 *ioc3;
	nasid_t nid;
        long loops;

	nid = get_nasid();
	ioc3 = (struct ioc3 *) KL_CONFIG_CH_CONS_INFO(nid)->memory_base;

	ioc3->sscr_a = 0;			/* PIO mode for uarta.  */
	ioc3->sscr_b = 0;			/* PIO mode for uartb.  */
	ioc3->sio_iec = ~0;
	ioc3->sio_ies = (SIO_IR_SA_INT | SIO_IR_SB_INT);

	loops=1000000; while(loops--);
	ioc3->sregs.uarta.iu_fcr = 0;
	ioc3->sregs.uartb.iu_fcr = 0;
	loops=1000000; while(loops--);
}

static inline void ioc3_eth_init(void)
{
	struct ioc3 *ioc3;
	nasid_t nid;

	nid = get_nasid();
	ioc3 = (struct ioc3 *) KL_CONFIG_CH_CONS_INFO(nid)->memory_base;

	ioc3->eier = 0;
}

/* Try to catch kernel missconfigurations and give user an indication what
   option to select.  */
static void __init verify_mode(void)
{
	int n_mode;

	n_mode = LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_MORENODES_MASK;
	printk("Machine is in %c mode.\n", n_mode ? 'N' : 'M');
#ifdef CONFIG_SGI_SN0_N_MODE
	if (!n_mode)
		panic("Kernel compiled for M mode.");
#else
	if (n_mode)
		panic("Kernel compiled for N mode.");
#endif
}

#define XBOW_WIDGET_PART_NUM    0x0
#define XXBOW_WIDGET_PART_NUM   0xd000  /* Xbow in Xbridge */
#define BASE_XBOW_PORT  	8     /* Lowest external port */

extern int bridge_probe(nasid_t nasid, int widget, int masterwid);

static int __init probe_one_port(nasid_t nasid, int widget, int masterwid)
{
	widgetreg_t 		widget_id;
	xwidget_part_num_t	partnum;

	widget_id = *(volatile widgetreg_t *)
		(RAW_NODE_SWIN_BASE(nasid, widget) + WIDGET_ID);
	partnum = XWIDGET_PART_NUM(widget_id);

	printk(KERN_INFO "Cpu %d, Nasid 0x%x, widget 0x%x (partnum 0x%x) is ",
			smp_processor_id(), nasid, widget, partnum);

	switch (partnum) {
	case BRIDGE_WIDGET_PART_NUM:
	case XBRIDGE_WIDGET_PART_NUM:
		bridge_probe(nasid, widget, masterwid);
		break;
	default:
		break;
	}

	return 0;
}

static int __init xbow_probe(nasid_t nasid)
{
	lboard_t *brd;
	klxbow_t *xbow_p;
	unsigned masterwid, i;

	printk("is xbow\n");

	/*
	 * found xbow, so may have multiple bridges
	 * need to probe xbow
	 */
	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_MIDPLANE8);
	if (!brd)
		return -ENODEV;
	
	xbow_p = (klxbow_t *)find_component(brd, NULL, KLSTRUCT_XBOW);
	if (!xbow_p)
		return -ENODEV;

	/*
	 * Okay, here's a xbow. Lets arbitrate and find
	 * out if we should initialize it. Set enabled
	 * hub connected at highest or lowest widget as
	 * master.
	 */
#ifdef WIDGET_A
	i = HUB_WIDGET_ID_MAX + 1;
	do {
		i--;
	} while ((!XBOW_PORT_TYPE_HUB(xbow_p, i)) ||
		 (!XBOW_PORT_IS_ENABLED(xbow_p, i)));
#else
	i = HUB_WIDGET_ID_MIN - 1;
	do {
		i++;
	} while ((!XBOW_PORT_TYPE_HUB(xbow_p, i)) ||
		 (!XBOW_PORT_IS_ENABLED(xbow_p, i)));
#endif

	masterwid = i;
	if (nasid != XBOW_PORT_NASID(xbow_p, i))
		return 1;

	for (i = HUB_WIDGET_ID_MIN; i <= HUB_WIDGET_ID_MAX; i++) {
		if (XBOW_PORT_IS_ENABLED(xbow_p, i) &&
		    XBOW_PORT_TYPE_IO(xbow_p, i))
			probe_one_port(nasid, i, masterwid);
	}

	return 0;
}

static spinlock_t pcibr_setup_lock = SPIN_LOCK_UNLOCKED;

void __init pcibr_setup(cnodeid_t nid)
{
	volatile u64 		hubreg;
	nasid_t	 		nasid;
	xwidget_part_num_t	partnum;
	widgetreg_t 		widget_id;


	spin_lock(&pcibr_setup_lock);

	/*
	 * If the master is doing this for headless node, nothing to do.
	 * This is because currently we require at least one of the hubs
	 * (master hub) connected to the xbow to have at least one enabled
	 * cpu to receive intrs. Else we need an array bus_to_intrnasid[]
	 * that bridge_startup() needs to use to target intrs. All dma is
	 * routed thru the widget of the master hub. The master hub wid
	 * is selectable by WIDGET_A below.
	 */
	if (nid != get_compact_nodeid())
		goto out;

	/* find what's on our local node */
	nasid = COMPACT_TO_NASID_NODEID(nid);
	hubreg = REMOTE_HUB_L(nasid, IIO_LLP_CSR);

	/* check whether the link is up */
	if (!(hubreg & IIO_LLP_CSR_IS_UP))
		goto out;

	widget_id = *(volatile widgetreg_t *)
                       (RAW_NODE_SWIN_BASE(nasid, 0x0) + WIDGET_ID);
	partnum = XWIDGET_PART_NUM(widget_id);

	printk(KERN_INFO "Cpu %d, Nasid 0x%x: partnum 0x%x is ",
			smp_processor_id(), nasid, partnum);

	switch (partnum) {
	case BRIDGE_WIDGET_PART_NUM:
		bridge_probe(nasid, 0x8, 0xa);
		break;
	case XBOW_WIDGET_PART_NUM:
	case XXBOW_WIDGET_PART_NUM:
		xbow_probe(nasid);
		break;
	default:
		printk(" unknown widget??\n");
		break;
	}

 out:
	spin_unlock(&pcibr_setup_lock);
}

extern void ip27_setup_console(void);
extern void ip27_time_init(void);
extern void ip27_reboot_setup(void);

void __init per_cpu_init(void)
{
	cnodeid_t cnode = get_compact_nodeid();
	int cpu = smp_processor_id();

	clear_c0_status(ST0_IM);
	per_hub_init(cnode);
	cpu_time_init();
	install_ipi();
	/* Install our NMI handler if symmon hasn't installed one. */
	install_cpu_nmi_handler(cputoslice(cpu));
	set_c0_status(SRB_DEV0 | SRB_DEV1);
}

static int __init ip27_setup(void)
{
	hubreg_t p, e;
	nasid_t nid;

	ip27_setup_console();
	ip27_reboot_setup();

	/*
	 * hub_rtc init and cpu clock intr enabled for later calibrate_delay.
	 */
	DBG("ip27_setup(): Entered.\n");
	nid = get_nasid();
	printk("IP27: Running on node %d.\n", nid);

	p = LOCAL_HUB_L(PI_CPU_PRESENT_A) & 1;
	e = LOCAL_HUB_L(PI_CPU_ENABLE_A) & 1;
	printk("Node %d has %s primary CPU%s.\n", nid,
	       p ? "a" : "no",
	       e ? ", CPU is running" : "");

	p = LOCAL_HUB_L(PI_CPU_PRESENT_B) & 1;
	e = LOCAL_HUB_L(PI_CPU_ENABLE_B) & 1;
	printk("Node %d has %s secondary CPU%s.\n", nid,
	       p ? "a" : "no",
	       e ? ", CPU is running" : "");

	verify_mode();
	ioc3_sio_init();
	ioc3_eth_init();
	per_cpu_init();

	set_io_port_base(IO_BASE);

	board_time_init = ip27_time_init;

	return 0;
}

early_initcall(ip27_setup);

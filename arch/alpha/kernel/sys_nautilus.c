/*
 *	linux/arch/alpha/kernel/sys_nautilus.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1998 Richard Henderson
 *	Copyright (C) 1999 Alpha Processor, Inc.,
 *		(David Daniel, Stig Telfer, Soohoon Lee)
 *
 * Code supporting NAUTILUS systems.
 *
 *
 * NAUTILUS has the following I/O features:
 *
 * a) Driven by AMD 751 aka IRONGATE (northbridge):
 *     4 PCI slots
 *     1 AGP slot
 *
 * b) Driven by ALI M1543C (southbridge)
 *     2 ISA slots
 *     2 IDE connectors
 *     1 dual drive capable FDD controller
 *     2 serial ports
 *     1 ECP/EPP/SP parallel port
 *     2 USB ports
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/reboot.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pci.h>
#include <asm/pgtable.h>
#include <asm/core_irongate.h>
#include <asm/hwrpb.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"


static void __init
nautilus_init_irq(void)
{
	if (alpha_using_srm) {
		alpha_mv.device_interrupt = srm_device_interrupt;
		alpha_mv.kill_arch = NULL;
	}

	init_i8259a_irqs();
	common_init_isa_dma();
}

static int __init
nautilus_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	/* Preserve the IRQ set up by the console.  */

	u8 irq;
	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
	return irq;
}

void
nautilus_kill_arch(int mode)
{
	switch (mode) {
	case LINUX_REBOOT_CMD_RESTART:
		{
			u8 t8;
			pcibios_read_config_byte(0, 0x38, 0x43, &t8);
			pcibios_write_config_byte(0, 0x38, 0x43, t8 | 0x80);
			outb(1, 0x92);
			outb(0, 0x92);
			/* NOTREACHED */
		}
		break;

	case LINUX_REBOOT_CMD_POWER_OFF:
		{
			u32 pmuport;
			pcibios_read_config_dword(0, 0x88, 0x10, &pmuport);
			pmuport &= 0xfffe;
			outl(0xffff, pmuport); /* clear pending events */
			outw(0x2000, pmuport+4); /* power off */
			/* NOTREACHED */
		}
		break;
	}
}

/* Machine check handler code
 *
 * Perform analysis of a machine check that was triggered by the EV6
 * CPU's fault-detection mechanism.
 */

/* IPR structures for EV6, containing the necessary data for the
 * machine check handler to unpick the logout frame
 */

/* I_STAT */

#define EV6__I_STAT__PAR                ( 1 << 29 )

/* MM_STAT */

#define EV6__MM_STAT__DC_TAG_PERR       ( 1 << 10 )

/* DC_STAT */

#define EV6__DC_STAT__SEO               ( 1 << 4 )
#define EV6__DC_STAT__ECC_ERR_LD        ( 1 << 3 )
#define EV6__DC_STAT__ECC_ERR_ST        ( 1 << 2 )
#define EV6__DC_STAT__TPERR_P1          ( 1 << 1 )
#define EV6__DC_STAT__TPERR_P0          ( 1      )

/* C_STAT */

#define EV6__C_STAT__BC_PERR            ( 0x01 )
#define EV6__C_STAT__DC_PERR            ( 0x02 )
#define EV6__C_STAT__DSTREAM_MEM_ERR    ( 0x03 )
#define EV6__C_STAT__DSTREAM_BC_ERR     ( 0x04 )
#define EV6__C_STAT__DSTREAM_DC_ERR     ( 0x05 )
#define EV6__C_STAT__PROBE_BC_ERR0      ( 0x06 )
#define EV6__C_STAT__PROBE_BC_ERR1      ( 0x07 )
#define EV6__C_STAT__ISTREAM_MEM_ERR    ( 0x0B )
#define EV6__C_STAT__ISTREAM_BC_ERR     ( 0x0C )
#define EV6__C_STAT__DSTREAM_MEM_DBL    ( 0x13 )
#define EV6__C_STAT__DSTREAM_BC_DBL     ( 0x14 )
#define EV6__C_STAT__ISTREAM_MEM_DBL    ( 0x1B )
#define EV6__C_STAT__ISTREAM_BC_DBL     ( 0x1C )


/* Take the two syndromes from the CBOX error chain and convert them
 * into a bit number.  */

/* NOTE - since I don't know of any difference between C0 and C1 I
   just ignore C1, since in all cases I've seen so far they are
   identical.  */

static const unsigned char ev6_bit_to_syndrome[72] =
{
	0xce, 0xcb, 0xd3, 0xd5, 0xd6, 0xd9, 0xda, 0xdc,     /* 0 */
	0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x31, 0x34,     /* 8 */
	0x0e, 0x0b, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,     /* 16 */
	0xe3, 0xe5, 0xe6, 0xe9, 0xea, 0xec, 0xf1, 0xf4,     /* 24 */
	0x4f, 0x4a, 0x52, 0x54, 0x57, 0x58, 0x5b, 0x5d,     /* 32 */
	0xa2, 0xa4, 0xa7, 0xa8, 0xab, 0xad, 0xb0, 0xb5,     /* 40 */
	0x8f, 0x8a, 0x92, 0x94, 0x97, 0x98, 0x9b, 0x9d,     /* 48 */
	0x62, 0x64, 0x67, 0x68, 0x6b, 0x6d, 0x70, 0x75,     /* 56 */
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80      /* 64 */
};


static int ev6_syn2bit(unsigned long c0, unsigned long c1)
{
	int bit;

	for (bit = 0; bit < 72; bit++)
		if (ev6_bit_to_syndrome[bit] == c0)	return bit;
	for (bit = 0; bit < 72; bit++)
		if (ev6_bit_to_syndrome[bit] == c1)	return bit + 64;

	return -1;                  /* not found */
}


/* Single bit ECC errors are categorized here.  */

#if 0
static const char *interr = "CPU internal error";
static const char *slotb= "Slot-B error";
static const char *membus= "Memory/EV6-bus error";
#else
static const char *interr = "";
static const char *slotb = "";
static const char *membus = "";
#endif

static void
ev6_crd_interp(char *interp, struct el_common_EV6_mcheck * L)
{
	/* Icache data or tag parity error.  */
	if (L->I_STAT & EV6__I_STAT__PAR) {
		sprintf(interp, "%s: I_STAT[PAR]\n "
			"Icache data or tag parity error", interr);
		return;
	}

	/* Dcache tag parity error (on issue) (DFAULT).  */
	if (L->MM_STAT & EV6__MM_STAT__DC_TAG_PERR) {
		sprintf(interp, "%s: MM_STAT[DC_TAG_PERR]\n "
			"Dcache tag parity error(on issue)", interr);
		return;
	}

	/* Errors relating to D-stream set non-zero DC_STAT.
	   Mask CRD bits.  */
	switch (L->DC_STAT & (EV6__DC_STAT__ECC_ERR_ST
			      | EV6__DC_STAT__ECC_ERR_LD)) {
	case EV6__DC_STAT__ECC_ERR_ST:
		/* Dcache single-bit ECC error on small store */
		sprintf(interp, "%s: DC_STAT[ECC_ERR_ST]\n "
			"Dcache single-bit ECC error on small store", interr);
		return;

	case EV6__DC_STAT__ECC_ERR_LD:
		switch (L->C_STAT) {
		case 0:
			/* Dcache single-bit error on speculative load */
			/* Bcache victim read on Dcache/Bcache miss */
			sprintf(interp, "%s: DC_STAT[ECC_ERR_LD] C_STAT=0\n "
				"Dcache single-bit ECC error on speculative load",
				slotb);
			return;

		case EV6__C_STAT__DSTREAM_DC_ERR:
			/* Dcache single bit error on load */
			sprintf(interp, "%s: DC_STAT[ECC_ERR_LD] C_STAT[DSTREAM_DC_ERR]\n"
				" Dcache single-bit ECC error on speculative load, bit %d",
				interr, ev6_syn2bit(L->DC0_SYNDROME, L->DC1_SYNDROME));
			return;

		case EV6__C_STAT__DSTREAM_BC_ERR:
			/* Bcache single-bit error on Dcache fill */
			sprintf(interp, "%s: DC_STAT[ECC_ERR_LD] C_STAT[DSTREAM_BC_ERR]\n"
				" Bcache single-bit error on Dcache fill, bit %d",
				slotb, ev6_syn2bit(L->DC0_SYNDROME, L->DC1_SYNDROME));
			return;

		case EV6__C_STAT__DSTREAM_MEM_ERR:
			/* Memory single-bit error on Dcache fill */
			sprintf(interp, "%s (to Dcache): DC_STAT[ECC_ERR_LD] "
				"C_STAT[DSTREAM_MEM_ERR]\n "
				"Memory single-bit error on Dcache fill, "
				"Address 0x%lX, bit %d",
				membus, L->C_ADDR, ev6_syn2bit(L->DC0_SYNDROME,
							       L->DC1_SYNDROME));
			return;
		}
	}

	/* I-stream, other misc errors go on C_STAT alone */
	switch (L->C_STAT) {
	case EV6__C_STAT__ISTREAM_BC_ERR:
		/* Bcache single-bit error on Icache fill (also MCHK) */
		sprintf(interp, "%s: C_STAT[ISTREAM_BC_ERR]\n "
			"Bcache single-bit error on Icache fill, bit %d",
			slotb, ev6_syn2bit(L->DC0_SYNDROME, L->DC1_SYNDROME));
		return;

	case EV6__C_STAT__ISTREAM_MEM_ERR:
		/* Memory single-bit error on Icache fill (also MCHK) */
		sprintf(interp, "%s : C_STATISTREAM_MEM_ERR]\n "
			"Memory single-bit error on Icache fill "
			"addr 0x%lX, bit %d",
			membus, L->C_ADDR, ev6_syn2bit(L->DC0_SYNDROME,
						       L->DC1_SYNDROME));
		return;

	case EV6__C_STAT__PROBE_BC_ERR0:
	case EV6__C_STAT__PROBE_BC_ERR1:
		/* Bcache single-bit error on a probe hit */
		sprintf(interp, "%s: C_STAT[PROBE_BC_ERR]\n "
			"Bcache single-bit error on a probe hit, "
			"addr 0x%lx, bit %d",
			slotb, L->C_ADDR, ev6_syn2bit(L->DC0_SYNDROME,
						      L->DC1_SYNDROME));
		return;
	}
}

static void
ev6_mchk_interp(char *interp, struct el_common_EV6_mcheck * L)
{
	/* Machine check errors described by DC_STAT */
	switch (L->DC_STAT) {
	case EV6__DC_STAT__TPERR_P0:
	case EV6__DC_STAT__TPERR_P1:
		/* Dcache tag parity error (on retry) */
		sprintf(interp, "%s: DC_STAT[TPERR_P0|TPERR_P1]\n "
			"Dcache tag parity error(on retry)", interr);
		return;

	case EV6__DC_STAT__SEO:
		/* Dcache second error on store */
		sprintf(interp, "%s: DC_STAT[SEO]\n "
			"Dcache second error during mcheck", interr);
		return;
	}

	/* Machine check errors described by C_STAT */
	switch (L->C_STAT) {
	case EV6__C_STAT__DC_PERR:
		/* Dcache duplicate tag parity error */
		sprintf(interp, "%s: C_STAT[DC_PERR]\n "
			"Dcache duplicate tag parity error at 0x%lX",
			interr, L->C_ADDR);
		return;

	case EV6__C_STAT__BC_PERR:
		/* Bcache tag parity error */
		sprintf(interp, "%s: C_STAT[BC_PERR]\n "
			"Bcache tag parity error at 0x%lX",
			slotb, L->C_ADDR);
		return;

	case EV6__C_STAT__ISTREAM_BC_ERR:
		/* Bcache single-bit error on Icache fill (also CRD) */
		sprintf(interp, "%s: C_STAT[ISTREAM_BC_ERR]\n "
			"Bcache single-bit error on Icache fill 0x%lX bit %d",
			slotb, L->C_ADDR,
			ev6_syn2bit(L->DC0_SYNDROME, L->DC1_SYNDROME));
		return;


	case EV6__C_STAT__ISTREAM_MEM_ERR:
		/* Memory single-bit error on Icache fill (also CRD) */
		sprintf(interp, "%s: C_STAT[ISTREAM_MEM_ERR]\n "
			"Memory single-bit error on Icache fill 0x%lX, bit %d",
			membus, L->C_ADDR,
			ev6_syn2bit(L->DC0_SYNDROME, L->DC1_SYNDROME));
		return;


	case EV6__C_STAT__ISTREAM_BC_DBL:
		/* Bcache double-bit error on Icache fill */
		sprintf(interp, "%s: C_STAT[ISTREAM_BC_DBL]\n "
			"Bcache double-bit error on Icache fill at 0x%lX",
			slotb, L->C_ADDR);
		return;
	case EV6__C_STAT__DSTREAM_BC_DBL:
		/* Bcache double-bit error on Dcache fill */
		sprintf(interp, "%s: C_STAT[DSTREAM_BC_DBL]\n "
			"Bcache double-bit error on Dcache fill at 0x%lX",
			slotb, L->C_ADDR);
		return;

	case EV6__C_STAT__ISTREAM_MEM_DBL:
		/* Memory double-bit error on Icache fill */
		sprintf(interp, "%s: C_STAT[ISTREAM_MEM_DBL]\n "
			"Memory double-bit error on Icache fill at 0x%lX",
			membus, L->C_ADDR);
		return;

	case EV6__C_STAT__DSTREAM_MEM_DBL:
		/* Memory double-bit error on Dcache fill */
		sprintf(interp, "%s: C_STAT[DSTREAM_MEM_DBL]\n "
			"Memory double-bit error on Dcache fill at 0x%lX",
			membus, L->C_ADDR);
		return;
	}
}

static void
ev6_cpu_machine_check(unsigned long vector, struct el_common_EV6_mcheck *L,
		      struct pt_regs *regs)
{
	char interp[80];

	/* This is verbose and looks intimidating.  Should it be printed for
	   corrected (CRD) machine checks? */

	printk(KERN_CRIT "PALcode logout frame:  "
	       "MCHK_Code       %d  "
	       "MCHK_Frame_Rev  %d\n"
	       "I_STAT  %016lx  "
	       "DC_STAT %016lx  "
	       "C_ADDR  %016lx\n"
	       "SYND1   %016lx  "
	       "SYND0   %016lx  "
	       "C_STAT  %016lx\n"
	       "C_STS   %016lx  "
	       "RES     %016lx  "
	       "EXC_ADDR%016lx\n"
	       "IER_CM  %016lx  "
	       "ISUM    %016lx  "
	       "MM_STAT %016lx\n"
	       "PALBASE %016lx  "
	       "I_CTL   %016lx  "
	       "PCTX    %016lx\n"
	       "CPU registers: "
	       "PC      %016lx  "
	       "Return  %016lx\n",
	       L->MCHK_Code, L->MCHK_Frame_Rev, L->I_STAT, L->DC_STAT,
	       L->C_ADDR, L->DC1_SYNDROME, L->DC0_SYNDROME, L->C_STAT,
	       L->C_STS, L->RESERVED0, L->EXC_ADDR, L->IER_CM, L->ISUM,
	       L->MM_STAT, L->PAL_BASE, L->I_CTL, L->PCTX,
	       regs->pc, regs->r26);

	/* Attempt an interpretation on the meanings of the fields above.  */
	sprintf(interp, "No interpretation available!" );
	if (vector == SCB_Q_PROCERR)
		ev6_crd_interp(interp, L);
	else if (vector == SCB_Q_PROCMCHK)
		ev6_mchk_interp(interp, L);

	printk(KERN_CRIT "interpretation: %s\n\n", interp);
}


/* Perform analysis of a machine check that arrived from the system (NMI) */

static void
naut_sys_machine_check(unsigned long vector, unsigned long la_ptr,
		       struct pt_regs *regs)
{
	printk("xtime %lx\n", CURRENT_TIME);
	printk("PC %lx RA %lx\n", regs->pc, regs->r26);
	irongate_pci_clr_err();
}

/* Machine checks can come from two sources - those on the CPU and those
   in the system.  They are analysed separately but all starts here.  */

void
nautilus_machine_check(unsigned long vector, unsigned long la_ptr,
		       struct pt_regs *regs)
{
	char *mchk_class;
	unsigned cpu_analysis=0, sys_analysis=0;

	/* Now for some analysis.  Machine checks fall into two classes --
	   those picked up by the system, and those picked up by the CPU.
	   Add to that the two levels of severity - correctable or not.  */

	if (vector == SCB_Q_SYSMCHK
	    && ((IRONGATE0->dramms & 0x300) == 0x300)) {
		unsigned long nmi_ctl;

		/* Clear ALI NMI */
		nmi_ctl = inb(0x61);
		nmi_ctl |= 0x0c;
		outb(nmi_ctl, 0x61);
		nmi_ctl &= ~0x0c;
		outb(nmi_ctl, 0x61);

		/* Write again clears error bits.  */
		IRONGATE0->stat_cmd = IRONGATE0->stat_cmd & ~0x100;
		mb();
		IRONGATE0->stat_cmd;

		/* Write again clears error bits.  */
		IRONGATE0->dramms = IRONGATE0->dramms;
		mb();
		IRONGATE0->dramms;

		draina();
		wrmces(0x7);
		mb();
		return;
	}

	switch (vector) {
	case SCB_Q_SYSERR:
		mchk_class = "Correctable System Machine Check (NMI)";
		sys_analysis = 1;
		break;
	case SCB_Q_SYSMCHK:
		mchk_class = "Fatal System Machine Check (NMI)";
		sys_analysis = 1;
		break;

	case SCB_Q_PROCERR:
		mchk_class = "Correctable Processor Machine Check";
		cpu_analysis = 1;
		break;
	case SCB_Q_PROCMCHK:
		mchk_class = "Fatal Processor Machine Check";
		cpu_analysis = 1;
		break;

	default:
		mchk_class = "Unknown vector!";
		break;
	}

	printk(KERN_CRIT "NAUTILUS Machine check 0x%lx [%s]\n",
	       vector, mchk_class);

	if (cpu_analysis)
		ev6_cpu_machine_check(vector,
				      (struct el_common_EV6_mcheck *)la_ptr,
				      regs);
	if (sys_analysis)
		naut_sys_machine_check(vector, la_ptr, regs);

	/* Tell the PALcode to clear the machine check */
	draina();
	wrmces(0x7);
	mb();
}



/*
 * The System Vectors
 */

struct alpha_machine_vector nautilus_mv __initmv = {
	vector_name:		"Nautilus",
	DO_EV6_MMU,
	DO_DEFAULT_RTC,
	DO_IRONGATE_IO,
	DO_IRONGATE_BUS,
	machine_check:		nautilus_machine_check,
	max_dma_address:	ALPHA_NAUTILUS_MAX_DMA_ADDRESS,
	min_io_address:		DEFAULT_IO_BASE,
	min_mem_address:	DEFAULT_MEM_BASE,

	nr_irqs:		16,
	device_interrupt:	isa_device_interrupt,

	init_arch:		irongate_init_arch,
	init_irq:		nautilus_init_irq,
	init_rtc:		common_init_rtc,
	init_pci:		common_init_pci,
	kill_arch:		nautilus_kill_arch,
	pci_map_irq:		nautilus_map_irq,
	pci_swizzle:		common_swizzle,
};
ALIAS_MV(nautilus)

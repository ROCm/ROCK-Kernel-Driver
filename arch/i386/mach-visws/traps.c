/* VISWS traps */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/debugreg.h>
#include <asm/desc.h>
#include <asm/i387.h>

#include <asm/smp.h>
#include <asm/pgalloc.h>
#include <asm/arch_hooks.h>

#ifdef CONFIG_X86_VISWS_APIC
#include <asm/fixmap.h>
#include <asm/cobalt.h>
#include <asm/lithium.h>
#endif

#ifdef CONFIG_X86_VISWS_APIC

/*
 * On Rev 005 motherboards legacy device interrupt lines are wired directly
 * to Lithium from the 307.  But the PROM leaves the interrupt type of each
 * 307 logical device set appropriate for the 8259.  Later we'll actually use
 * the 8259, but for now we have to flip the interrupt types to
 * level triggered, active lo as required by Lithium.
 */

#define	REG	0x2e	/* The register to read/write */
#define	DEV	0x07	/* Register: Logical device select */
#define	VAL	0x2f	/* The value to read/write */

static void
superio_outb(int dev, int reg, int val)
{
	outb(DEV, REG);
	outb(dev, VAL);
	outb(reg, REG);
	outb(val, VAL);
}

static int __attribute__ ((unused))
superio_inb(int dev, int reg)
{
	outb(DEV, REG);
	outb(dev, VAL);
	outb(reg, REG);
	return inb(VAL);
}

#define	FLOP	3	/* floppy logical device */
#define	PPORT	4	/* parallel logical device */
#define	UART5	5	/* uart2 logical device (not wired up) */
#define	UART6	6	/* uart1 logical device (THIS is the serial port!) */
#define	IDEST	0x70	/* int. destination (which 307 IRQ line) reg. */
#define	ITYPE	0x71	/* interrupt type register */

/* interrupt type bits */
#define	LEVEL	0x01	/* bit 0, 0 == edge triggered */
#define	ACTHI	0x02	/* bit 1, 0 == active lo */

static __init void
superio_init(void)
{
	if (visws_board_type == VISWS_320 && visws_board_rev == 5) {
		superio_outb(UART6, IDEST, 0);	/* 0 means no intr propagated */
		printk("SGI 320 rev 5: disabling 307 uart1 interrupt\n");
	}
}

static __init void
lithium_init(void)
{
	set_fixmap(FIX_LI_PCIA, LI_PCI_A_PHYS);
	printk("Lithium PCI Bridge A, Bus Number: %d\n",
				li_pcia_read16(LI_PCI_BUSNUM) & 0xff);
	set_fixmap(FIX_LI_PCIB, LI_PCI_B_PHYS);
	printk("Lithium PCI Bridge B (PIIX4), Bus Number: %d\n",
				li_pcib_read16(LI_PCI_BUSNUM) & 0xff);

	/* XXX blindly enables all interrupts */
	li_pcia_write16(LI_PCI_INTEN, 0xffff);
	li_pcib_write16(LI_PCI_INTEN, 0xffff);
}

static __init void
cobalt_init(void)
{
	/*
	 * On normal SMP PC this is used only with SMP, but we have to
	 * use it and set it up here to start the Cobalt clock
	 */
	set_fixmap(FIX_APIC_BASE, APIC_DEFAULT_PHYS_BASE);
	printk("Local APIC ID %lx\n", apic_read(APIC_ID));
	printk("Local APIC Version %lx\n", apic_read(APIC_LVR));

	set_fixmap(FIX_CO_CPU, CO_CPU_PHYS);
	printk("Cobalt Revision %lx\n", co_cpu_read(CO_CPU_REV));

	set_fixmap(FIX_CO_APIC, CO_APIC_PHYS);
	printk("Cobalt APIC ID %lx\n", co_apic_read(CO_APIC_ID));

	/* Enable Cobalt APIC being careful to NOT change the ID! */
	co_apic_write(CO_APIC_ID, co_apic_read(CO_APIC_ID)|CO_APIC_ENABLE);

	printk("Cobalt APIC enabled: ID reg %lx\n", co_apic_read(CO_APIC_ID));
}
#endif

void __init trap_init_hook()
{
#ifdef CONFIG_X86_VISWS_APIC
	superio_init();
	lithium_init();
	cobalt_init();
#endif
}

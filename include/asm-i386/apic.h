#ifndef __ASM_APIC_H
#define __ASM_APIC_H

#include <linux/config.h>
#include <asm/apicdef.h>
#include <asm/system.h>

#define APIC_DEBUG 1

#ifdef CONFIG_X86_LOCAL_APIC

#if APIC_DEBUG
#define Dprintk(x...) printk(x)
#else
#define Dprintk(x...)
#endif

/*
 * Basic functions accessing APICs.
 */

extern __inline void apic_write(unsigned long reg, unsigned long v)
{
	*((volatile unsigned long *)(APIC_BASE+reg)) = v;
}

extern __inline void apic_write_atomic(unsigned long reg, unsigned long v)
{
	xchg((volatile unsigned long *)(APIC_BASE+reg), v);
}

extern __inline unsigned long apic_read(unsigned long reg)
{
	return *((volatile unsigned long *)(APIC_BASE+reg));
}

static __inline__ void apic_wait_icr_idle(void)
{
	do { } while ( apic_read( APIC_ICR ) & APIC_ICR_BUSY );
}

extern unsigned int apic_timer_irqs [NR_CPUS];

#ifdef CONFIG_X86_GOOD_APIC
# define FORCE_READ_AROUND_WRITE 0
# define apic_read_around(x)
# define apic_write_around(x,y) apic_write((x),(y))
#else
# define FORCE_READ_AROUND_WRITE 1
# define apic_read_around(x) apic_read(x)
# define apic_write_around(x,y) apic_write_atomic((x),(y))
#endif

extern inline void ack_APIC_irq(void)
{
	/*
	 * ack_APIC_irq() actually gets compiled as a single instruction:
	 * - a single rmw on Pentium/82489DX
	 * - a single write on P6+ cores (CONFIG_X86_GOOD_APIC)
	 * ... yummie.
	 */

	/* Docs say use 0 for future compatibility */
	apic_write_around(APIC_EOI, 0);
}

extern int get_maxlvt(void);
extern void connect_bsp_APIC (void);
extern void disconnect_bsp_APIC (void);
extern void disable_local_APIC (void);
extern int verify_local_APIC (void);
extern void cache_APIC_registers (void);
extern void sync_Arb_IDs(void);
extern void setup_local_APIC (void);
extern void init_apic_mappings(void);
extern void smp_local_timer_interrupt(struct pt_regs * regs);
extern void setup_APIC_clocks(void);
#endif

#endif

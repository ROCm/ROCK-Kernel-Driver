/*
 *  linux/arch/i386/kernel/time_hpet.c
 *  This code largely copied from arch/x86_64/kernel/time.c
 *  See that file for credits.
 *
 *  2003-06-30    Venkatesh Pallipadi - Additional changes for HPET support
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/timer.h>
#include <asm/fixmap.h>
#include <asm/apic.h>

#include <linux/timex.h>
#include <linux/config.h>

#include <asm/hpet.h>

unsigned long hpet_period;	/* fsecs / HPET clock */
unsigned long hpet_tick;	/* hpet clks count per tick */
unsigned long hpet_address;	/* hpet memory map physical address */

static int use_hpet; 		/* can be used for runtime check of hpet */
static int boot_hpet_disable; 	/* boottime override for HPET timer */
static unsigned long hpet_virt_address;	/* hpet kernel virtual address */

#define FSEC_TO_USEC (1000000000UL)

int hpet_readl(unsigned long a)
{
	return readl(hpet_virt_address + a);
}

void hpet_writel(unsigned long d, unsigned long a)
{
	writel(d, hpet_virt_address + a);
}

#ifdef CONFIG_X86_LOCAL_APIC
/*
 * HPET counters dont wrap around on every tick. They just change the
 * comparator value and continue. Next tick can be caught by checking
 * for a change in the comparator value. Used in apic.c.
 */
void __init wait_hpet_tick(void)
{
	unsigned int start_cmp_val, end_cmp_val;

	start_cmp_val = hpet_readl(HPET_T0_CMP);
	do {
		end_cmp_val = hpet_readl(HPET_T0_CMP);
	} while (start_cmp_val == end_cmp_val);
}
#endif

/*
 * Check whether HPET was found by ACPI boot parse. If yes setup HPET
 * counter 0 for kernel base timer.
 */
int __init hpet_enable(void)
{
	unsigned int cfg, id;
	unsigned long tick_fsec_low, tick_fsec_high; /* tick in femto sec */
	unsigned long hpet_tick_rem;

	if (boot_hpet_disable)
		return -1;

	if (!hpet_address) {
		return -1;
	}
	hpet_virt_address = (unsigned long) ioremap_nocache(hpet_address,
	                                                    HPET_MMAP_SIZE);
	/*
	 * Read the period, compute tick and quotient.
	 */
	id = hpet_readl(HPET_ID);

	/*
	 * We are checking for value '1' or more in number field.
	 * So, we are OK with HPET_EMULATE_RTC part too, where we need
	 * to have atleast 2 timers.
	 */
	if (!(id & HPET_ID_NUMBER) ||
	    !(id & HPET_ID_LEGSUP))
		return -1;

	if (((id & HPET_ID_VENDOR) >> HPET_ID_VENDOR_SHIFT) !=
				HPET_ID_VENDOR_8086)
		return -1;

	hpet_period = hpet_readl(HPET_PERIOD);
	if ((hpet_period < HPET_MIN_PERIOD) || (hpet_period > HPET_MAX_PERIOD))
		return -1;

	/*
	 * 64 bit math
	 * First changing tick into fsec
	 * Then 64 bit div to find number of hpet clk per tick
	 */
	ASM_MUL64_REG(tick_fsec_low, tick_fsec_high,
			KERNEL_TICK_USEC, FSEC_TO_USEC);
	ASM_DIV64_REG(hpet_tick, hpet_tick_rem,
			hpet_period, tick_fsec_low, tick_fsec_high);

	if (hpet_tick_rem > (hpet_period >> 1))
		hpet_tick++; /* rounding the result */

	/*
	 * Stop the timers and reset the main counter.
	 */
	cfg = hpet_readl(HPET_CFG);
	cfg &= ~HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);
	hpet_writel(0, HPET_COUNTER);
	hpet_writel(0, HPET_COUNTER + 4);

	/*
	 * Set up timer 0, as periodic with first interrupt to happen at
	 * hpet_tick, and period also hpet_tick.
	 */
	cfg = hpet_readl(HPET_T0_CFG);
	cfg |= HPET_TN_ENABLE | HPET_TN_PERIODIC |
	       HPET_TN_SETVAL | HPET_TN_32BIT;
	hpet_writel(cfg, HPET_T0_CFG);
	hpet_writel(hpet_tick, HPET_T0_CMP);

	/*
 	 * Go!
 	 */
	cfg = hpet_readl(HPET_CFG);
	cfg |= HPET_CFG_ENABLE | HPET_CFG_LEGACY;
	hpet_writel(cfg, HPET_CFG);

	use_hpet = 1;
#ifdef CONFIG_X86_LOCAL_APIC
	wait_timer_tick = wait_hpet_tick;
#endif
	return 0;
}

int is_hpet_enabled(void)
{
	return use_hpet;
}

int is_hpet_capable(void)
{
	if (!boot_hpet_disable && hpet_address)
		return 1;
	return 0;
}

static int __init hpet_setup(char* str)
{
	if (str) {
		if (!strncmp("disable", str, 7))
			boot_hpet_disable = 1;
	}
	return 1;
}

__setup("hpet=", hpet_setup);



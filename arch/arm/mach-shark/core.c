/*
 *  linux/arch/arm/mach-shark/arch.c
 *
 *  Architecture specific stuff.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/io.h>
#include <asm/leds.h>
#include <asm/param.h>

#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

extern void shark_init_irq(void);

static struct map_desc shark_io_desc[] __initdata = {
	{ IO_BASE	, IO_START	, IO_SIZE	, MT_DEVICE }
};

static void __init shark_map_io(void)
{
	iotable_init(shark_io_desc, ARRAY_SIZE(shark_io_desc));
}

#define IRQ_TIMER 0
#define HZ_TIME ((1193180 + HZ/2) / HZ)

static irqreturn_t
shark_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_leds();
	do_timer(regs);
	do_profile(regs);

	return IRQ_HANDLED;
}

static struct irqaction shark_timer_irq = {
	.name		= "Shark Timer Tick",
	.flags		= SA_INTERRUPT,
	.handler	= shark_timer_interrupt
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
void __init shark_init_time(void)
{
        unsigned long flags;

	outb(0x34, 0x43);               /* binary, mode 0, LSB/MSB, Ch 0 */
	outb(HZ_TIME & 0xff, 0x40);     /* LSB of count */
	outb(HZ_TIME >> 8, 0x40);

	setup_irq(IRQ_TIMER, &shark_timer_irq);
}


MACHINE_START(SHARK, "Shark")
	MAINTAINER("Alexander Schulz")
	BOOT_MEM(0x08000000, 0x40000000, 0xe0000000)
	BOOT_PARAMS(0x08003000)
	MAPIO(shark_map_io)
	INITIRQ(shark_init_irq)
	INITTIME(shark_init_time)
MACHINE_END

/*
 * Setup kernel for a Sun3x machine
 *
 * (C) 1999 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 *
 * based on code from Oliver Jowett <oliver@jowett.manawatu.gen.nz>
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/sun3x.h>

#include "time.h"

static volatile unsigned char *sun3x_intreg = (unsigned char *)SUN3X_INTREG;
extern int serial_console;

void sun3x_halt(void)
{
    /* Disable interrupts */
    cli();

    /* we can't drop back to PROM, so we loop here */
    for (;;);
}

void sun3x_reboot(void)
{
    /* This never returns, don't bother saving things */
    cli();

    /* no idea, whether this works */
    asm ("reset");
}

int __init sun3x_keyb_init(void)
{
    return 0;
}

int sun3x_kbdrate(struct kbd_repeat *r)
{
    return 0;
}

void sun3x_kbd_leds(unsigned int i)
{

}

static void sun3x_badint (int irq, void *dev_id, struct pt_regs *fp)
{
    printk ("received spurious interrupt %d\n",irq);
    num_spurious += 1;
}

void (*sun3x_default_handler[SYS_IRQS])(int, void *, struct pt_regs *) = {
    sun3x_badint, sun3x_badint, sun3x_badint, sun3x_badint,
    sun3x_badint, sun3x_badint, sun3x_badint, sun3x_badint
};

void sun3x_enable_irq(unsigned int irq)
{
    *sun3x_intreg |= (1 << irq);
}

void sun3x_disable_irq(unsigned int irq)
{
    *sun3x_intreg &= ~(1 << irq);
}

void __init sun3x_init_IRQ(void)
{
    /* disable all interrupts initially */
    *sun3x_intreg = 1;  /* master enable only */
}

int sun3x_get_irq_list(char *buf)
{
    return 0;
}

/*
 *  Setup the sun3x configuration info
 */
void __init config_sun3x(void)
{
    mach_get_irq_list	 = sun3x_get_irq_list;
    mach_max_dma_address = 0xffffffff; /* we can DMA anywhere, whee */

    mach_keyb_init       = sun3x_keyb_init;
    mach_kbdrate         = sun3x_kbdrate;
    mach_kbd_leds        = sun3x_kbd_leds;

    mach_sched_init      = sun3x_sched_init;
    mach_init_IRQ        = sun3x_init_IRQ;
    enable_irq           = sun3x_enable_irq;
    disable_irq          = sun3x_disable_irq;
    mach_request_irq     = sys_request_irq;
    mach_free_irq        = sys_free_irq;
    mach_default_handler = &sun3x_default_handler;
    mach_gettimeoffset   = sun3x_gettimeoffset;
    mach_reset           = sun3x_reboot;

    mach_gettod          = sun3x_gettod;
    
    switch (*(unsigned char *)SUN3X_EEPROM_CONS) {
	case 0x10:
	    serial_console = 1;
	    conswitchp = NULL;
	    break;
	case 0x11:
	    serial_console = 2;
	    conswitchp = NULL;
	    break;
	default:
	    serial_console = 0;
	    conswitchp = &dummy_con;
	    break;
    }

}

/*
 *  linux/arch/m68k/sun3x/time.c
 *
 *  Sun3x-specific time handling
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/sun3x.h>

#include "time.h"

#define M_CONTROL 0xf8
#define M_SEC     0xf9
#define M_MIN     0xfa
#define M_HOUR    0xfb
#define M_DAY     0xfc
#define M_DATE    0xfd
#define M_MONTH   0xfe
#define M_YEAR    0xff

#define C_WRITE   0x80
#define C_READ    0x40
#define C_SIGN    0x20
#define C_CALIB   0x1f

#define BCD_TO_BIN(val) (((val)&15) + ((val)>>4)*10)

/* Read the Mostek */
void sun3x_gettod (int *yearp, int *monp, int *dayp,
                   int *hourp, int *minp, int *secp)
{
    volatile unsigned char *eeprom = (unsigned char *)SUN3X_EEPROM;

    /* Stop updates */
    *(eeprom + M_CONTROL) |= C_READ;

    /* Read values */
    *yearp = BCD_TO_BIN(*(eeprom + M_YEAR));
    *monp  = BCD_TO_BIN(*(eeprom + M_MONTH));
    *dayp  = BCD_TO_BIN(*(eeprom + M_DATE));
    *hourp = BCD_TO_BIN(*(eeprom + M_HOUR));
    *minp  = BCD_TO_BIN(*(eeprom + M_MIN));
    *secp  = BCD_TO_BIN(*(eeprom + M_SEC));

    /* Restart updates */
    *(eeprom + M_CONTROL) &= ~C_READ;
}

/* Not much we can do here */
unsigned long sun3x_gettimeoffset (void)
{
    return 0L;
}

static void sun3x_timer_tick(int irq, void *dev_id, struct pt_regs *regs)
{
    void (*vector)(int, void *, struct pt_regs *) = dev_id;

    /* Clear the pending interrupt - pulse the enable line low */
    disable_irq(5);
    enable_irq(5);
    
    vector(irq, NULL, regs);
}

void __init sun3x_sched_init(void (*vector)(int, void *, struct pt_regs *))
{
    sys_request_irq(5, sun3x_timer_tick, IRQ_FLG_STD, "timer tick", vector);

    /* Pulse enable low to get the clock started */
    disable_irq(5);
    enable_irq(5);
}

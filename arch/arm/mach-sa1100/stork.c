/*
 * linux/arch/arm/mach-sa1100/stork.c
 *
 *     Copyright (C) 2001 Ken Gordon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/delay.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/keyboard.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <linux/serial_core.h>

#include "generic.h"


#define STORK_VM_BASE_CS1 0xf0000000		/* where we get mapped (virtual) */
#define STORK_VM_OFF_CS1 0x08000000             /* where we started mapping (physical) */
#define STORK_VM_ADJUST_CS1 (STORK_VM_BASE_CS1-STORK_VM_OFF_CS1) /* add to the phys to get virt */

#define STORK_VM_BASE_CS2 0xf1000000		/* where we get mapped (virtual) */
#define STORK_VM_OFF_CS2  0x10000000             /* where we started mapping (physical) */
#define STORK_VM_ADJUST_CS2 (STORK_VM_BASE_CS2-STORK_VM_OFF_CS2) /* add to the phys to get virt */

static int debug = 0;

static int storkLatchA = 0;
static int storkLatchB = 0;
static int storkLCDCPLD[4] = { 0, 0, 0, 0};

int
storkSetLatchA(int bits)
{
    int ret = storkLatchA;
    volatile unsigned int *latch = (unsigned int *)(STORK_LATCH_A_ADDR+STORK_VM_ADJUST_CS1);

    storkLatchA |= bits;
    *latch = storkLatchA;
    return ret;
}

int
storkClearLatchA(int bits)
{
    int ret = storkLatchA;
    volatile unsigned int *latch = (unsigned int *)(STORK_LATCH_A_ADDR+STORK_VM_ADJUST_CS1);

    storkLatchA &= ~bits;
    *latch = storkLatchA;
    return ret;
}

int
storkSetLCDCPLD(int which, int bits)
{
    int ret = storkLCDCPLD[which];
    volatile unsigned int *latch = (unsigned int *)(STORK_LCDCPLD_BASE_ADDR+STORK_VM_ADJUST_CS2 + 0x20*which);

    storkLCDCPLD[which] |= bits;
    *latch = storkLCDCPLD[which];
    return ret;
}


/* NB we don't shadow these 'cos there is no relation between the data written and the data read */
/* ie the read registers are read only and the write registers write only */

int
storkGetLCDCPLD(int which)
{
    volatile unsigned int *latch = (unsigned int *)(STORK_LCDCPLD_BASE_ADDR+STORK_VM_ADJUST_CS2 + 0x20*which);
    return *latch;
}

int
storkClearLCDCPLD(int which, int bits)
{
    int ret = storkLCDCPLD[which];
    volatile unsigned int *latch = (unsigned int *)(STORK_LCDCPLD_BASE_ADDR+STORK_VM_ADJUST_CS2 + 0x20*which);

    storkLCDCPLD[which] &= ~bits;
    *latch = storkLCDCPLD[which];
    return ret;
}

int
storkSetLatchB(int bits)
{
    int ret = storkLatchB;
    char buf[100];

    volatile unsigned int *latch = (unsigned int *)(STORK_LATCH_B_ADDR+STORK_VM_ADJUST_CS1);
    sprintf(buf, "%s: bits %04x\n", __FUNCTION__, bits);
    if (debug) printk(buf);

    storkLatchB |= bits;
    *latch = storkLatchB;
    return ret;
}

int
storkClearLatchB(int bits)
{
    int ret = storkLatchB;
    char buf[100];

    volatile unsigned int *latch = (unsigned int *)(STORK_LATCH_B_ADDR+STORK_VM_ADJUST_CS1);
    sprintf(buf, "%s: bits %04x\n", __FUNCTION__, bits);
    if (debug) printk(buf);

    storkLatchB &= ~bits;
    *latch = storkLatchB;
    return ret;
}

void
storkSetGPIO(int bits)
{
    char buf[100];

    sprintf(buf, "%s: bits %04x\n", __FUNCTION__, bits);
    if (debug) printk(buf);
    GPSR = bits;
}

void
storkClearGPIO(int bits)
{
    char buf[100];

    sprintf(buf, "%s: bits %04x\n", __FUNCTION__, bits);
    if (debug) printk(buf);
    GPCR = bits;
}

int
storkGetGPIO()
{
    char buf[100];

    int bits = GPLR;

    sprintf(buf, "%s: bits %04x\n", __FUNCTION__, bits);
    if (debug) printk(buf);

    return bits;
}

/* this will return the current state of the hardware ANDED with the given bits
   so NE => at least one bit was set, but maybe not all of them! */

int
storkTestGPIO(int bits)
{
    int val = storkGetGPIO();
    char buf[100];

    sprintf(buf, "%s: bits %04x val %04x\n", __FUNCTION__, bits, val);
    if (debug) printk(buf);

    return (val & bits);
}

/* NB the touch screen and the d to a use the same data and clock out pins */

static void storkClockTS(void)
{
    storkSetLatchB(STORK_TOUCH_SCREEN_DCLK);
    udelay(10);			 /* hmm wait 200ns (min) - ok this ought to be udelay(1) but that doesn't get */
				 /* consistent values so I'm using 10 (urgh) */
    storkClearLatchB(STORK_TOUCH_SCREEN_DCLK);
    udelay(10);
}


int				/* there is always a 12 bit read after the write! */
storkClockByteToTS(int byte)
{
    int timeout = 10000;   /* stuff is meant to happen in 60ns */
    int bit;
    int result = 0;

    if (debug) printk("storkClockByteToTS: %02x\n", byte);

    storkClearLatchB(STORK_TOUCH_SCREEN_CS);  /* slect touch screen */

    while (timeout-- > 0)
        if (storkTestGPIO(GPIO_STORK_TOUCH_SCREEN_BUSY) == 0)
            break;

    if (timeout < 0) {
        printk("storkClockBitToTS: GPIO_STORK_TOUCH_SCREEN_BUSY didn't go low!\n\r");
/* ignore error for now        return; */
    }

/* clock out the given byte */

    for (bit = 0x80; bit > 0; bit = bit >> 1) {

        if ((bit & byte) == 0)
            storkClearLatchB(STORK_TOUCH_SCREEN_DIN);
        else
            storkSetLatchB(STORK_TOUCH_SCREEN_DIN);

        storkClockTS();
    }

    storkClockTS();  /* will be busy for at a clock  (at least) */

    for (timeout = 10000; timeout >= 0; timeout--)
        if (storkTestGPIO(GPIO_STORK_TOUCH_SCREEN_BUSY) == 0)
            break;

    if (timeout < 0) {
        printk("storkClockBitToTS: 2nd GPIO_STORK_TOUCH_SCREEN_BUSY didn't go low!\n\r");
/* ignore error for now        return; */
    }

/* clock in the result */

    for (bit = 0x0800; bit > 0; bit = bit >> 1) {

        if (storkTestGPIO(GPIO_STORK_TOUCH_SCREEN_DATA))
            result |= bit;

        storkClockTS();
    }

    storkSetLatchB(STORK_TOUCH_SCREEN_CS);  /* unselect touch screen */

    return result;
}

void
storkClockShortToDtoA(int word)
{
    int bit;

    storkClearLatchB(STORK_DA_CS);  /* select D to A */

/* clock out the given byte */

    for (bit = 0x8000; bit > 0; bit = bit >> 1) {

        if ((bit & word) == 0)
            storkClearLatchB(STORK_TOUCH_SCREEN_DIN);
        else
            storkSetLatchB(STORK_TOUCH_SCREEN_DIN);

        storkClockTS();
    }

    storkSetLatchB(STORK_DA_CS);  /* unselect D to A */

/* set DTOA#_LOAD low then high (min 20ns) to transfer value to D to A */
    storkClearLatchB(STORK_DA_LD);
    storkSetLatchB(STORK_DA_LD);
}



void
storkInitTSandDtoA(void)
{
    storkClearLatchB(STORK_TOUCH_SCREEN_DCLK | STORK_TOUCH_SCREEN_DIN);
    storkSetLatchB(STORK_TOUCH_SCREEN_CS | STORK_DA_CS | STORK_DA_LD);
    storkClockByteToTS(0xE2);	 	/* turn on the reference */
    storkClockShortToDtoA(0x8D00);	/* turn on the contrast */
    storkClockShortToDtoA(0x0A00);	/* turn on the brightness */
}

static void stork_lcd_power(int on)
{
	if (on) {
		storkSetLCDCPLD(0, 1);
		storkSetLatchA(STORK_LCD_BACKLIGHT_INVERTER_ON);
	} else {
		storkSetLCDCPLD(0, 0);
		storkClearLatchA(STORK_LCD_BACKLIGHT_INVERTER_ON);
	}
}

struct map_desc stork_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { STORK_VM_BASE_CS1, STORK_VM_OFF_CS1, 0x01000000, MT_DEVICE }, /* EGPIO 0 */
  { 0xf1000000, 0x10000000, 0x02800000, MT_DEVICE }, /* static memory bank 2 */
  { 0xf3800000, 0x40000000, 0x00800000, MT_DEVICE }  /* static memory bank 4 */
};

int __init
stork_map_io(void)
{
    sa1100_map_io();
    iotable_init(stork_io_desc, ARRAY_SIZE(stork_io_desc));

    sa1100_register_uart(0, 1);	/* com port */
    sa1100_register_uart(1, 2);
    sa1100_register_uart(2, 3);

    printk("Stork driver initing latches\r\n");

    storkClearLatchB(STORK_RED_LED);	/* let's have the red LED on please */
    storkSetLatchB(STORK_YELLOW_LED);
    storkSetLatchB(STORK_GREEN_LED);
    storkSetLatchA(STORK_BATTERY_CHARGER_ON);
    storkSetLatchA(STORK_LCD_5V_POWER_ON);
    storkSetLatchA(STORK_LCD_3V3_POWER_ON);

    storkInitTSandDtoA();

    sa1100fb_lcd_power = stork_lcd_power;

    return 0;
}


MACHINE_START(STORK, "Stork Technologies prototype")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	MAPIO(stork_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END


EXPORT_SYMBOL(storkTestGPIO);
EXPORT_SYMBOL(storkSetGPIO);
EXPORT_SYMBOL(storkClearGPIO);
EXPORT_SYMBOL(storkSetLatchA);
EXPORT_SYMBOL(storkClearLatchA);
EXPORT_SYMBOL(storkSetLatchB);
EXPORT_SYMBOL(storkClearLatchB);
EXPORT_SYMBOL(storkClockByteToTS);
EXPORT_SYMBOL(storkClockShortToDtoA);
EXPORT_SYMBOL(storkGetLCDCPLD);
EXPORT_SYMBOL(storkSetLCDCPLD);

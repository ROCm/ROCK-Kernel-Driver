/* linux/include/asm/hardware/s3c2410/
 *
 * Copyright (c) 2003 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 GPIO register definitions
 *
 *  Changelog:
 *    19-06-2003     BJD     Created file
 *    23-06-2003     BJD     Updated GSTATUS registers
 *    12-03-2004     BJD     Updated include protection
 */


#ifndef __ASM_ARCH_REGS_GPIO_H
#define __ASM_ARCH_REGS_GPIO_H "$Id: gpio.h,v 1.5 2003/05/19 12:51:08 ben Exp $"

/* configure GPIO ports A..G */

#define S3C2410_GPIOREG(x) ((x) + S3C2410_VA_GPIO)

/* port A - 22bits, zero in bit X makes pin X output
 * 1 makes port special function, this is default
*/
#define S3C2410_GPACON	   S3C2410_GPIOREG(0x00)
#define S3C2410_GPADAT	   S3C2410_GPIOREG(0x04)

/* 0x08 and 0x0c are reserved */

/* GPB is 10 IO pins, each configured by 2 bits each in GPBCON.
 *   00 = input, 01 = output, 10=special function, 11=reserved
 * bit 0,1 = pin 0, 2,3= pin 1...
 *
 * CPBUP = pull up resistor control, 1=disabled, 0=enabled
*/

#define S3C2410_GPBCON	   S3C2410_GPIOREG(0x10)
#define S3C2410_GPBDAT	   S3C2410_GPIOREG(0x14)
#define S3C2410_GPBUP	   S3C2410_GPIOREG(0x18)

/* no i/o pin in port b can have value 3! */

#define S3C2410_GPB0_INP     (0x00 << 0)
#define S3C2410_GPB0_OUTP    (0x01 << 0)
#define S3C2410_GPB0_TOUT0   (0x02 << 0)

#define S3C2410_GPB1_INP     (0x00 << 2)
#define S3C2410_GPB1_OUTP    (0x01 << 2)
#define S3C2410_GPB1_TOUT1   (0x02 << 2)

#define S3C2410_GPB2_INP     (0x00 << 4)
#define S3C2410_GPB2_OUTP    (0x01 << 4)
#define S3C2410_GPB2_TOUT2   (0x02 << 4)

#define S3C2410_GPB3_INP     (0x00 << 6)
#define S3C2410_GPB3_OUTP    (0x01 << 6)
#define S3C2410_GPB3_TOUT3   (0x02 << 6)

#define S3C2410_GPB4_INP     (0x00 << 8)
#define S3C2410_GPB4_OUTP    (0x01 << 8)
#define S3C2410_GPB4_TCLK0   (0x02 << 8)
#define S3C2410_GPB4_MASK    (0x03 << 8)

#define S3C2410_GPB5_INP     (0x00 << 10)
#define S3C2410_GPB5_OUTP    (0x01 << 10)
#define S3C2410_GPB5_nXBACK  (0x02 << 10)

#define S3C2410_GPB6_INP     (0x00 << 12)
#define S3C2410_GPB6_OUTP    (0x01 << 12)
#define S3C2410_GPB6_nXBREQ  (0x02 << 12)

#define S3C2410_GPB7_INP     (0x00 << 14)
#define S3C2410_GPB7_OUTP    (0x01 << 14)
#define S3C2410_GPB7_nXDACK1 (0x02 << 14)

#define S3C2410_GPB8_INP     (0x00 << 16)
#define S3C2410_GPB8_OUTP    (0x01 << 16)
#define S3C2410_GPB8_nXDREQ1 (0x02 << 16)

#define S3C2410_GPB9_INP     (0x00 << 18)
#define S3C2410_GPB9_OUTP    (0x01 << 18)
#define S3C2410_GPB9_nXDACK0 (0x02 << 18)

#define S3C2410_GPB10_INP     (0x00 << 18)
#define S3C2410_GPB10_OUTP    (0x01 << 18)
#define S3C2410_GPB10_nXDRE0 (0x02 << 18)

/* Port C consits of 16 GPIO/Special function
 *
 * almost identical setup to port b, but the special functions are mostly
 * to do with the video system's sync/etc.
*/

#define S3C2410_GPCCON	   S3C2410_GPIOREG(0x20)
#define S3C2410_GPCDAT	   S3C2410_GPIOREG(0x24)
#define S3C2410_GPCUP	   S3C2410_GPIOREG(0x28)

#define S3C2410_GPC0_INP	(0x00 << 0)
#define S3C2410_GPC0_OUTP	(0x01 << 0)
#define S3C2410_GPC0_LEND	(0x02 << 0)

#define S3C2410_GPC1_INP	(0x00 << 2)
#define S3C2410_GPC1_OUTP	(0x01 << 2)
#define S3C2410_GPC1_VCLK	(0x02 << 2)

#define S3C2410_GPC2_INP	(0x00 << 4)
#define S3C2410_GPC2_OUTP	(0x01 << 4)
#define S3C2410_GPC2_VLINE	(0x02 << 4)

#define S3C2410_GPC3_INP	(0x00 << 6)
#define S3C2410_GPC3_OUTP	(0x01 << 6)
#define S3C2410_GPC3_VFRAME	(0x02 << 6)

#define S3C2410_GPC4_INP	(0x00 << 8)
#define S3C2410_GPC4_OUTP	(0x01 << 8)
#define S3C2410_GPC4_VM		(0x02 << 8)

#define S3C2410_GPC5_INP	(0x00 << 10)
#define S3C2410_GPC5_OUTP	(0x01 << 10)
#define S3C2410_GPC5_LCDVF0	(0x02 << 10)

#define S3C2410_GPC6_INP	(0x00 << 12)
#define S3C2410_GPC6_OUTP	(0x01 << 12)
#define S3C2410_GPC6_LCDVF1	(0x02 << 12)

#define S3C2410_GPC7_INP	(0x00 << 14)
#define S3C2410_GPC7_OUTP	(0x01 << 14)
#define S3C2410_GPC7_LCDVF2	(0x02 << 14)

#define S3C2410_GPC8_INP	(0x00 << 16)
#define S3C2410_GPC8_OUTP	(0x01 << 16)
#define S3C2410_GPC8_VD0	(0x02 << 16)

#define S3C2410_GPC9_INP	(0x00 << 18)
#define S3C2410_GPC9_OUTP	(0x01 << 18)
#define S3C2410_GPC9_VD1	(0x02 << 18)

#define S3C2410_GPC10_INP	(0x00 << 20)
#define S3C2410_GPC10_OUTP	(0x01 << 20)
#define S3C2410_GPC10_VD2	(0x02 << 20)

#define S3C2410_GPC11_INP	(0x00 << 22)
#define S3C2410_GPC11_OUTP	(0x01 << 22)
#define S3C2410_GPC11_VD3	(0x02 << 22)

#define S3C2410_GPC12_INP	(0x00 << 24)
#define S3C2410_GPC12_OUTP	(0x01 << 24)
#define S3C2410_GPC12_VD4	(0x02 << 24)

#define S3C2410_GPC13_INP	(0x00 << 26)
#define S3C2410_GPC13_OUTP	(0x01 << 26)
#define S3C2410_GPC13_VD5	(0x02 << 26)

#define S3C2410_GPC14_INP	(0x00 << 28)
#define S3C2410_GPC14_OUTP	(0x01 << 28)
#define S3C2410_GPC14_VD6	(0x02 << 28)

#define S3C2410_GPC15_INP	(0x00 << 30)
#define S3C2410_GPC15_OUTP	(0x01 << 30)
#define S3C2410_GPC15_VD7	(0x02 << 30)

/* Port D consists of 16 GPIO/Special function
 *
 * almost identical setup to port b, but the special functions are mostly
 * to do with the video system's data.
*/

#define S3C2410_GPDCON	   S3C2410_GPIOREG(0x30)
#define S3C2410_GPDDAT	   S3C2410_GPIOREG(0x34)
#define S3C2410_GPDUP	   S3C2410_GPIOREG(0x38)

#define S3C2410_GPD0_INP	(0x00 << 0)
#define S3C2410_GPD0_OUTP	(0x01 << 0)
#define S3C2410_GPD0_VD8	(0x02 << 0)

#define S3C2410_GPD1_INP	(0x00 << 2)
#define S3C2410_GPD1_OUTP	(0x01 << 2)
#define S3C2410_GPD1_VD9	(0x02 << 2)

#define S3C2410_GPD2_INP	(0x00 << 4)
#define S3C2410_GPD2_OUTP	(0x01 << 4)
#define S3C2410_GPD2_VD10	(0x02 << 4)

#define S3C2410_GPD3_INP	(0x00 << 6)
#define S3C2410_GPD3_OUTP	(0x01 << 6)
#define S3C2410_GPD3_VD11	(0x02 << 6)

#define S3C2410_GPD4_INP	(0x00 << 8)
#define S3C2410_GPD4_OUTP	(0x01 << 8)
#define S3C2410_GPD4_VD12	(0x02 << 8)

#define S3C2410_GPD5_INP	(0x00 << 10)
#define S3C2410_GPD5_OUTP	(0x01 << 10)
#define S3C2410_GPD5_VD13	(0x02 << 10)

#define S3C2410_GPD6_INP	(0x00 << 12)
#define S3C2410_GPD6_OUTP	(0x01 << 12)
#define S3C2410_GPD6_VD14	(0x02 << 12)

#define S3C2410_GPD7_INP	(0x00 << 14)
#define S3C2410_GPD7_OUTP	(0x01 << 14)
#define S3C2410_GPD7_VD15	(0x02 << 14)

#define S3C2410_GPD8_INP	(0x00 << 16)
#define S3C2410_GPD8_OUTP	(0x01 << 16)
#define S3C2410_GPD8_VD16	(0x02 << 16)

#define S3C2410_GPD9_INP	(0x00 << 18)
#define S3C2410_GPD9_OUTP	(0x01 << 18)
#define S3C2410_GPD9_VD17	(0x02 << 18)

#define S3C2410_GPD10_INP	(0x00 << 20)
#define S3C2410_GPD10_OUTP	(0x01 << 20)
#define S3C2410_GPD10_VD18	(0x02 << 20)

#define S3C2410_GPD11_INP	(0x00 << 22)
#define S3C2410_GPD11_OUTP	(0x01 << 22)
#define S3C2410_GPD11_VD19	(0x02 << 22)

#define S3C2410_GPD12_INP	(0x00 << 24)
#define S3C2410_GPD12_OUTP	(0x01 << 24)
#define S3C2410_GPD12_VD20	(0x02 << 24)

#define S3C2410_GPD13_INP	(0x00 << 26)
#define S3C2410_GPD13_OUTP	(0x01 << 26)
#define S3C2410_GPD13_VD21	(0x02 << 26)

#define S3C2410_GPD14_INP	(0x00 << 28)
#define S3C2410_GPD14_OUTP	(0x01 << 28)
#define S3C2410_GPD14_VD22	(0x02 << 28)

#define S3C2410_GPD15_INP	(0x00 << 30)
#define S3C2410_GPD15_OUTP	(0x01 << 30)
#define S3C2410_GPD15_VD23	(0x02 << 30)

/* Port E consists of 16 GPIO/Special function
 *
 * again, the same as port B, but dealing with I2S, SDI, and
 * more miscellaneous functions
*/

#define S3C2410_GPECON	   S3C2410_GPIOREG(0x40)
#define S3C2410_GPEDAT	   S3C2410_GPIOREG(0x44)
#define S3C2410_GPEUP	   S3C2410_GPIOREG(0x48)

#define S3C2410_GPE0_INP       (0x00 << 0)
#define S3C2410_GPE0_OUTP      (0x01 << 0)
#define S3C2410_GPE0_I2SLRCK   (0x02 << 0)
#define S3C2410_GPE0_MASK      (0x03 << 0)

#define S3C2410_GPE1_INP       (0x00 << 2)
#define S3C2410_GPE1_OUTP      (0x01 << 2)
#define S3C2410_GPE1_I2SSCLK   (0x02 << 2)
#define S3C2410_GPE1_MASK      (0x03 << 2)

#define S3C2410_GPE2_INP       (0x00 << 4)
#define S3C2410_GPE2_OUTP      (0x01 << 4)
#define S3C2410_GPE2_CDCLK     (0x02 << 4)

#define S3C2410_GPE3_INP       (0x00 << 6)
#define S3C2410_GPE3_OUTP      (0x01 << 6)
#define S3C2410_GPE3_I2SSDI    (0x02 << 6)
#define S3C2410_GPE3_MASK      (0x03 << 6)

#define S3C2410_GPE4_INP       (0x00 << 8)
#define S3C2410_GPE4_OUTP      (0x01 << 8)
#define S3C2410_GPE4_I2SSDO    (0x02 << 8)
#define S3C2410_GPE4_MASK      (0x03 << 8)

#define S3C2410_GPE5_INP       (0x00 << 10)
#define S3C2410_GPE5_OUTP      (0x01 << 10)
#define S3C2410_GPE5_SDCLK     (0x02 << 10)

#define S3C2410_GPE6_INP       (0x00 << 12)
#define S3C2410_GPE6_OUTP      (0x01 << 12)
#define S3C2410_GPE6_SDCLK     (0x02 << 12)

#define S3C2410_GPE7_INP       (0x00 << 14)
#define S3C2410_GPE7_OUTP      (0x01 << 14)
#define S3C2410_GPE7_SDCMD     (0x02 << 14)

#define S3C2410_GPE8_INP       (0x00 << 16)
#define S3C2410_GPE8_OUTP      (0x01 << 16)
#define S3C2410_GPE8_SDDAT1    (0x02 << 16)

#define S3C2410_GPE9_INP       (0x00 << 18)
#define S3C2410_GPE9_OUTP      (0x01 << 18)
#define S3C2410_GPE9_SDDAT2    (0x02 << 18)

#define S3C2410_GPE10_INP      (0x00 << 20)
#define S3C2410_GPE10_OUTP     (0x01 << 20)
#define S3C2410_GPE10_SDDAT3   (0x02 << 20)

#define S3C2410_GPE11_INP      (0x00 << 22)
#define S3C2410_GPE11_OUTP     (0x01 << 22)
#define S3C2410_GPE11_SPIMISO0 (0x02 << 22)

#define S3C2410_GPE12_INP      (0x00 << 24)
#define S3C2410_GPE12_OUTP     (0x01 << 24)
#define S3C2410_GPE12_SPIMOSI0 (0x02 << 24)

#define S3C2410_GPE13_INP      (0x00 << 26)
#define S3C2410_GPE13_OUTP     (0x01 << 26)
#define S3C2410_GPE13_SPICLK0  (0x02 << 26)

#define S3C2410_GPE14_INP      (0x00 << 28)
#define S3C2410_GPE14_OUTP     (0x01 << 28)
#define S3C2410_GPE14_IICSCL   (0x02 << 28)
#define S3C2410_GPE14_MASK     (0x03 << 28)

#define S3C2410_GPE15_INP      (0x00 << 30)
#define S3C2410_GPE15_OUTP     (0x01 << 30)
#define S3C2410_GPE15_IICSDA   (0x02 << 30)
#define S3C2410_GPE15_MASK     (0x03 << 30)

#define S3C2410_GPE_PUPDIS(x)  (1<<(x))

/* Port F consists of 8 GPIO/Special function
 *
 * GPIO / interrupt inputs
 *
 * GPFCON has 2 bits for each of the input pins on port F
 *   00 = 0 input, 1 output, 2 interrupt (EINT0..7), 3 undefined
 *
 * pull up works like all other ports.
*/

#define S3C2410_GPFCON	   S3C2410_GPIOREG(0x50)
#define S3C2410_GPFDAT	   S3C2410_GPIOREG(0x54)
#define S3C2410_GPFUP	   S3C2410_GPIOREG(0x58)


#define S3C2410_GPF0_INP    (0x00 << 0)
#define S3C2410_GPF0_OUTP   (0x01 << 0)
#define S3C2410_GPF0_EINT0  (0x02 << 0)

#define S3C2410_GPF1_INP    (0x00 << 2)
#define S3C2410_GPF1_OUTP   (0x01 << 2)
#define S3C2410_GPF1_EINT1  (0x02 << 2)

#define S3C2410_GPF2_INP    (0x00 << 4)
#define S3C2410_GPF2_OUTP   (0x01 << 4)
#define S3C2410_GPF2_EINT2  (0x02 << 4)

#define S3C2410_GPF3_INP    (0x00 << 6)
#define S3C2410_GPF3_OUTP   (0x01 << 6)
#define S3C2410_GPF3_EINT3  (0x02 << 6)

#define S3C2410_GPF4_INP    (0x00 << 8)
#define S3C2410_GPF4_OUTP   (0x01 << 8)
#define S3C2410_GPF4_EINT4  (0x02 << 8)

#define S3C2410_GPF5_INP    (0x00 << 10)
#define S3C2410_GPF5_OUTP   (0x01 << 10)
#define S3C2410_GPF5_EINT5  (0x02 << 10)

#define S3C2410_GPF6_INP    (0x00 << 12)
#define S3C2410_GPF6_OUTP   (0x01 << 12)
#define S3C2410_GPF6_EINT6  (0x02 << 12)

#define S3C2410_GPF7_INP    (0x00 << 14)
#define S3C2410_GPF7_OUTP   (0x01 << 14)
#define S3C2410_GPF7_EINT7  (0x02 << 14)

/* Port G consists of 8 GPIO/IRQ/Special function
 *
 * GPGCON has 2 bits for each of the input pins on port F
 *   00 = 0 input, 1 output, 2 interrupt (EINT0..7), 3 special func
 *
 * pull up works like all other ports.
*/

#define S3C2410_GPGCON	   S3C2410_GPIOREG(0x60)
#define S3C2410_GPGDAT	   S3C2410_GPIOREG(0x64)
#define S3C2410_GPGUP	   S3C2410_GPIOREG(0x68)

#define S3C2410_GPG0_INP      (0x00 << 0)
#define S3C2410_GPG0_OUTP     (0x01 << 0)
#define S3C2410_GPG0_EINT8    (0x02 << 0)

#define S3C2410_GPG1_INP      (0x00 << 2)
#define S3C2410_GPG1_OUTP     (0x01 << 2)
#define S3C2410_GPG1_EINT9    (0x02 << 2)

#define S3C2410_GPG2_INP      (0x00 << 4)
#define S3C2410_GPG2_OUTP     (0x01 << 4)
#define S3C2410_GPG2_EINT10   (0x02 << 4)

#define S3C2410_GPG3_INP      (0x00 << 6)
#define S3C2410_GPG3_OUTP     (0x01 << 6)
#define S3C2410_GPG3_EINT11   (0x02 << 6)

#define S3C2410_GPG4_INP      (0x00 << 8)
#define S3C2410_GPG4_OUTP     (0x01 << 8)
#define S3C2410_GPG4_EINT12   (0x02 << 8)
#define S3C2410_GPG4_LCDPWREN (0x03 << 8)

#define S3C2410_GPG5_INP      (0x00 << 10)
#define S3C2410_GPG5_OUTP     (0x01 << 10)
#define S3C2410_GPG5_EINT13   (0x02 << 10)
#define S3C2410_GPG5_SPIMISO1 (0x03 << 10)

#define S3C2410_GPG6_INP      (0x00 << 12)
#define S3C2410_GPG6_OUTP     (0x01 << 12)
#define S3C2410_GPG6_EINT14   (0x02 << 12)
#define S3C2410_GPG6_SPIMOSI1 (0x03 << 12)

#define S3C2410_GPG7_INP      (0x00 << 14)
#define S3C2410_GPG7_OUTP     (0x01 << 14)
#define S3C2410_GPG7_EINT15   (0x02 << 14)
#define S3C2410_GPG7_SPICLK1  (0x03 << 14)

#define S3C2410_GPG8_INP      (0x00 << 16)
#define S3C2410_GPG8_OUTP     (0x01 << 16)
#define S3C2410_GPG8_EINT16   (0x02 << 16)

#define S3C2410_GPG9_INP      (0x00 << 18)
#define S3C2410_GPG9_OUTP     (0x01 << 18)
#define S3C2410_GPG9_EINT17   (0x02 << 18)

#define S3C2410_GPG10_INP     (0x00 << 20)
#define S3C2410_GPG10_OUTP    (0x01 << 20)
#define S3C2410_GPG10_EINT18  (0x02 << 20)

#define S3C2410_GPG11_INP     (0x00 << 22)
#define S3C2410_GPG11_OUTP    (0x01 << 22)
#define S3C2410_GPG11_EINT19  (0x02 << 22)
#define S3C2410_GPG11_TCLK1   (0x03 << 22)

#define S3C2410_GPG12_INP     (0x00 << 24)
#define S3C2410_GPG12_OUTP    (0x01 << 24)
#define S3C2410_GPG12_EINT18  (0x02 << 24)
#define S3C2410_GPG12_XMON    (0x03 << 24)

#define S3C2410_GPG13_INP     (0x00 << 26)
#define S3C2410_GPG13_OUTP    (0x01 << 26)
#define S3C2410_GPG13_EINT18  (0x02 << 26)
#define S3C2410_GPG13_nXPON   (0x03 << 26)

#define S3C2410_GPG14_INP     (0x00 << 28)
#define S3C2410_GPG14_OUTP    (0x01 << 28)
#define S3C2410_GPG14_EINT18  (0x02 << 28)
#define S3C2410_GPG14_YMON    (0x03 << 28)

#define S3C2410_GPG15_INP     (0x00 << 30)
#define S3C2410_GPG15_OUTP    (0x01 << 30)
#define S3C2410_GPG15_EINT18  (0x02 << 30)
#define S3C2410_GPG15_nYPON   (0x03 << 30)


#define S3C2410_GPG_PUPDIS(x)  (1<<(x))

/* Port H consists of11 GPIO/serial/Misc pins
 *
 * GPGCON has 2 bits for each of the input pins on port F
 *   00 = 0 input, 1 output, 2 interrupt (EINT0..7), 3 special func
 *
 * pull up works like all other ports.
*/

#define S3C2410_GPHCON	   S3C2410_GPIOREG(0x70)
#define S3C2410_GPHDAT	   S3C2410_GPIOREG(0x74)
#define S3C2410_GPHUP	   S3C2410_GPIOREG(0x78)

#define S3C2410_GPH0_INP    (0x00 << 0)
#define S3C2410_GPH0_OUTP   (0x01 << 0)
#define S3C2410_GPH0_nCTS0  (0x02 << 0)

#define S3C2410_GPH1_INP    (0x00 << 2)
#define S3C2410_GPH1_OUTP   (0x01 << 2)
#define S3C2410_GPH1_nRTS0  (0x02 << 2)

#define S3C2410_GPH2_INP    (0x00 << 4)
#define S3C2410_GPH2_OUTP   (0x01 << 4)
#define S3C2410_GPH2_TXD0   (0x02 << 4)

#define S3C2410_GPH3_INP    (0x00 << 6)
#define S3C2410_GPH3_OUTP   (0x01 << 6)
#define S3C2410_GPH3_RXD0   (0x02 << 6)

#define S3C2410_GPH4_INP    (0x00 << 8)
#define S3C2410_GPH4_OUTP   (0x01 << 8)
#define S3C2410_GPH4_TXD1   (0x02 << 8)

#define S3C2410_GPH5_INP    (0x00 << 10)
#define S3C2410_GPH5_OUTP   (0x01 << 10)
#define S3C2410_GPH5_RXD1   (0x02 << 10)

#define S3C2410_GPH6_INP    (0x00 << 12)
#define S3C2410_GPH6_OUTP   (0x01 << 12)
#define S3C2410_GPH6_TXD2   (0x02 << 12)
#define S3C2410_GPH6_nRTS1  (0x03 << 12)

#define S3C2410_GPH7_INP    (0x00 << 14)
#define S3C2410_GPH7_OUTP   (0x01 << 14)
#define S3C2410_GPH7_RXD2   (0x02 << 14)
#define S3C2410_GPH7_nCTS1  (0x03 << 14)

#define S3C2410_GPH8_INP    (0x00 << 16)
#define S3C2410_GPH8_OUTP   (0x01 << 16)
#define S3C2410_GPH8_UCLK   (0x02 << 16)

#define S3C2410_GPH9_INP     (0x00 << 18)
#define S3C2410_GPH9_OUTP    (0x01 << 18)
#define S3C2410_GPH9_CLKOUT0 (0x02 << 18)

#define S3C2410_GPH10_INP   (0x00 << 20)
#define S3C2410_GPH10_OUTP  (0x01 << 20)
#define S3C2410_GPH10_CLKOUT1  (0x02 << 20)

/* miscellaneous control */

#define S3C2410_MISCCR	   S3C2410_GPIOREG(0x80)
#define S3C2410_DCLKCON	   S3C2410_GPIOREG(0x84)

/* see clock.h for dclk definitions */

/* pullup control on databus */
#define S3C2410_MISCCR_SPUCR_HEN    (0)
#define S3C2410_MISCCR_SPUCR_HDIS   (1<<0)
#define S3C2410_MISCCR_SPUCR_LEN    (0)
#define S3C2410_MISCCR_SPUCR_LDIS   (1<<1)

#define S3C2410_MISCCR_USBDEV	    (0)
#define S3C2410_MISCCR_USBHOST	    (1<<3)

#define S3C2410_MISCCR_CLK0_MPLL    (0<<4)
#define S3C2410_MISCCR_CLK0_UPLL    (1<<4)
#define S3C2410_MISCCR_CLK0_FCLK    (2<<4)
#define S3C2410_MISCCR_CLK0_HCLK    (3<<4)
#define S3C2410_MISCCR_CLK0_PCLK    (4<<4)
#define S3C2410_MISCCR_CLK0_DCLK0   (5<<4)

#define S3C2410_MISCCR_CLK1_MPLL    (0<<8)
#define S3C2410_MISCCR_CLK1_UPLL    (1<<8)
#define S3C2410_MISCCR_CLK1_FCLK    (2<<8)
#define S3C2410_MISCCR_CLK1_HCLK    (3<<8)
#define S3C2410_MISCCR_CLK1_PCLK    (4<<8)
#define S3C2410_MISCCR_CLK1_DCLK1   (5<<8)

#define S3C2410_MISCCR_USBSUSPND0   (1<<12)
#define S3C2410_MISCCR_USBSUSPND1   (1<<13)

#define S3C2410_MISCCR_nRSTCON	    (1<<16)

/* external interrupt control... */
/* S3C2410_EXTINT0 -> irq sense control for EINT0..EINT7
 * S3C2410_EXTINT1 -> irq sense control for EINT8..EINT15
 * S3C2410_EXTINT2 -> irq sense control for EINT16..EINT23
 *
 * note S3C2410_EXTINT2 has filtering options for EINT16..EINT23
 *
 * Samsung datasheet p9-25
*/

#define S3C2410_EXTINT0	   S3C2410_GPIOREG(0x88)
#define S3C2410_EXTINT1	   S3C2410_GPIOREG(0x8C)
#define S3C2410_EXTINT2	   S3C2410_GPIOREG(0x90)

/* values for S3C2410_EXTINT0/1/2 */
#define S3C2410_EXTINT_LOWLEV	 (0x00)
#define S3C2410_EXTINT_HILEV	 (0x01)
#define S3C2410_EXTINT_FALLEDGE	 (0x02)
#define S3C2410_EXTINT_RISEEDGE	 (0x04)
#define S3C2410_EXTINT_BOTHEDGE	 (0x06)

/* interrupt filtering conrrol for EINT16..EINT23 */
#define S3C2410_EINFLT0	   S3C2410_GPIOREG(0x94)
#define S3C2410_EINFLT1	   S3C2410_GPIOREG(0x98)
#define S3C2410_EINFLT2	   S3C2410_GPIOREG(0x9C)
#define S3C2410_EINFLT3	   S3C2410_GPIOREG(0xA0)

/* mask: 0=enable, 1=disable
 * 1 bit EINT, 4=EINT4, 23=EINT23
 * EINT0,1,2,3 are not handled here.
*/
#define S3C2410_EINTMASK   S3C2410_GPIOREG(0xA4)
#define S3C2410_EINTPEND   S3C2410_GPIOREG(0xA8)

/* GSTATUS have miscellaneous information in them
 *
 */

#define S3C2410_GSTATUS0   S3C2410_GPIOREG(0x0AC)
#define S3C2410_GSTATUS1   S3C2410_GPIOREG(0x0B0)
#define S3C2410_GSTATUS2   S3C2410_GPIOREG(0x0B4)
#define S3C2410_GSTATUS3   S3C2410_GPIOREG(0x0B8)
#define S3C2410_GSTATUS4   S3C2410_GPIOREG(0x0BC)

#define S3C2410_GSTATUS0_nWAIT	   (1<<3)
#define S3C2410_GSTATUS0_NCON	   (1<<2)
#define S3C2410_GSTATUS0_RnB	   (1<<1)
#define S3C2410_GSTATUS0_nBATTFLT  (1<<0)

#define S3C2410_GSTATUS2_WTRESET   (1<<2)
#define S3C2410_GSTATUs2_OFFRESET  (1<<1)
#define S3C2410_GSTATUS2_PONRESET  (1<<0)

#endif	/* __ASM_ARCH_REGS_GPIO_H */


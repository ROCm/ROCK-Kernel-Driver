/* linux/include/asm/arch-s3c2410/regs-clock.h
 *
 * Copyright (c) 2003 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 clock register definitions
 *
 *  Changelog:
 *    19-06-2003     BJD     Created file
 *    12-03-2004     BJD     Updated include protection
 */



#ifndef __ASM_ARM_REGS_CLOCK
#define __ASM_ARM_REGS_CLOCK "$Id: clock.h,v 1.4 2003/04/30 14:50:51 ben Exp $"

#define S3C2410_CLKREG(x) ((x) + S3C2410_VA_CLKPWR)

#define S3C2410_PLLVAL(_m,_p,_s) ((_m) << 12 | ((_p) << 4) | ((_s)))

#define S3C2410_LOCKTIME    S3C2410_CLKREG(0x00)
#define S3C2410_MPLLCON	    S3C2410_CLKREG(0x04)
#define S3C2410_UPLLCON	    S3C2410_CLKREG(0x08)
#define S3C2410_CLKCON	    S3C2410_CLKREG(0x0C)
#define S3C2410_CLKSLOW	    S3C2410_CLKREG(0x10)
#define S3C2410_CLKDIVN	    S3C2410_CLKREG(0x14)

#define S3C2410_PLLCON_MDIVSHIFT     12
#define S3C2410_PLLCON_PDIVSHIFT     4
#define S3C2410_PLLCON_SDIVSHIFT     0
#define S3C2410_PLLCON_MDIVMASK	     ((1<<(1+(19-12)))-1)
#define S3C2410_PLLCON_PDIVMASK	     ((1<<5)-1)
#define S3C2410_PLLCON_SDIVMASK	     3

/* DCLKCON register addresses in gpio.h */

#define S3C2410_DCLKCON_DCLK0EN	     (1<<0)
#define S3C2410_DCLKCON_DCLK0_PCLK   (0<<1)
#define S3C2410_DCLKCON_DCLK0_UCLK   (1<<1)
#define S3C2410_DCLKCON_DCLK0_DIV(x) (((x) - 1 )<<4)
#define S3C2410_DCLKCON_DCLK0_CMP(x) (((x) - 1 )<<8)

#define S3C2410_DCLKCON_DCLK1EN	     (1<<16)
#define S3C2410_DCLKCON_DCLK1_PCLK   (0<<17)
#define S3C2410_DCLKCON_DCLK1_UCLK   (1<<17)
#define S3C2410_DCLKCON_DCLK1_DIV(x) (((x) - 1) <<20)

#define S3C2410_CLKDIVN_PDIVN	     (1<<0)
#define S3C2410_CLKDIVN_HDIVN	     (1<<1)

static inline unsigned int
s3c2410_get_pll(int pllval, int baseclk)
{
  int mdiv, pdiv, sdiv;

  mdiv = pllval >> S3C2410_PLLCON_MDIVSHIFT;
  pdiv = pllval >> S3C2410_PLLCON_PDIVSHIFT;
  sdiv = pllval >> S3C2410_PLLCON_SDIVSHIFT;

  mdiv &= S3C2410_PLLCON_MDIVMASK;
  pdiv &= S3C2410_PLLCON_PDIVMASK;
  sdiv &= S3C2410_PLLCON_SDIVMASK;

  return (baseclk * (mdiv + 8)) / ((pdiv + 2) << sdiv);
}

#endif /* __ASM_ARM_REGS_CLOCK */

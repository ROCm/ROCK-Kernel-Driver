/* arch/arm/mach-s3c2410/s3c2440.h
 *
 * Copyright (c) 2004 Simtec Electronics
 * Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for s3c2440 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *	24-Aug-2004 BJD  Start of S3C2440 CPU support
 *	04-Nov-2004 BJD  Added s3c2440_init_uarts()
*/

struct s3c2410_uartcfg;

extern void s3c2440_init_irq(void);

extern void s3c2440_init_time(void);

extern void s3c2440_init_uarts(struct s3c2410_uartcfg *cfg, int no);

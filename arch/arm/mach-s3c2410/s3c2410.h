/* arch/arm/mach-s3c2410/s3c2410.h
 *
 * Copyright (c) 2004 Simtec Electronics
 * Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for s3c2410 machine directory
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     18-Aug-2004 BJD  Created initial version
 *     20-Aug-2004 BJD  Added s3c2410_board struct
 *     04-Sep-2004 BJD  Added s3c2410_init_uarts() call
 *     17-Oct-2004 BJD  Moved board out to cpu
*/

struct s3c2410_uartcfg;

extern void s3c2410_map_io(struct map_desc *, int count);

extern void s3c2410_init_uarts(struct s3c2410_uartcfg *, int no);

extern void s3c2410_init_irq(void);

struct sys_timer;
extern struct sys_timer s3c2410_timer;

extern void s3c2410_init_uarts(struct s3c2410_uartcfg *cfg, int no);

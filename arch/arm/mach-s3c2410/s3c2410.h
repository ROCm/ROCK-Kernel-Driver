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
 *
*/

extern void s3c2410_map_io(struct map_desc *, int count);

extern void s3c2410_init_irq(void);

extern void s3c2410_init_time(void);


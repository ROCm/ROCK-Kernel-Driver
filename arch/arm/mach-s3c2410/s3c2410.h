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
*/

extern void s3c2410_map_io(struct map_desc *, int count);

extern void s3c2410_init_irq(void);

extern void s3c2410_init_time(void);

/* the board structure is used at first initialsation time
 * to get info such as the devices to register for this
 * board. This is done because platfrom_add_devices() cannot
 * be called from the map_io entry.
 *
*/

struct s3c2410_board {
	struct platform_device  **devices;
	unsigned int              devices_count;
};

extern void s3c2410_set_board(struct s3c2410_board *board);

extern void s3c2410_init_uarts(struct s3c2410_uartcfg *cfg, int no);

/*
 * linux/arch/arm/mach-s3c2410/clock.h
 *
 * Copyright (c) 2004 Simtec Electronics
 * Written by Ben Dooks, <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

struct clk {
	struct list_head      list;
	struct module        *owner;
	struct clk           *parent;
	const char           *name;
	atomic_t              used;
	unsigned long         rate;
	unsigned long         ctrlbit;
};

#ifndef _I8042_IO_H
#define _I8042_IO_H

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by 
 * the Free Software Foundation.
 */

/*
 * Names.
 */

#define I8042_KBD_PHYS_DESC "isa0060/serio0"
#define I8042_AUX_PHYS_DESC "isa0060/serio1"

/*
 * IRQs.
 */

#define I8042_KBD_IRQ CONFIG_I8042_KBD_IRQ 
#define I8042_AUX_IRQ CONFIG_I8042_AUX_IRQ

/*
 * Register numbers.
 */

#define I8042_COMMAND_REG	CONFIG_I8042_REG_BASE + 4	
#define I8042_STATUS_REG	CONFIG_I8042_REG_BASE + 4	
#define I8042_DATA_REG		CONFIG_I8042_REG_BASE	


static inline int i8042_read_data(void)
{
	return inb(I8042_DATA_REG);
}

static inline int i8042_read_status(void)
{
	return inb(I8042_STATUS_REG);
}

static inline void i8042_write_data(int val)
{
	outb(val, I8042_DATA_REG);
	return;
}

static inline void i8042_write_command(int val)
{
	outb(val, I8042_COMMAND_REG);
	return;
}

static inline int i8042_platform_init(void)
{
/*
 * On ix86 platforms touching the i8042 data register region can do really
 * bad things. Because of this the region is always reserved on ix86 boxes.
 */
#if !defined(__i386__) && !defined(__sh__) && !defined(__alpha__)
	if (!request_region(I8042_DATA_REG, 16, "i8042"))
		return 0;
#endif
	return 1;
}

static inline void i8042_platform_exit(void)
{
#if !defined(__i386__) && !defined(__sh__) && !defined(__alpha__)
	release_region(I8042_DATA_REG, 16);
#endif
}

#endif /* _I8042_IO_H */

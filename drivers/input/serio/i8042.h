#ifndef _I8042_H
#define _I8042_H

/*
 * $Id: i8042.h,v 1.6 2001/10/05 22:48:09 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

/*
 * If you want to trace all the i/o the i8042 module does for
 * debugging purposes, define this.
 */

#undef I8042_DEBUG_IO

/*
 * On most PC based systems the keyboard IRQ is 1.
 */

#define I8042_KBD_IRQ CONFIG_I8042_KBD_IRQ 

/*
 * On most PC based systems the aux port IRQ is 12. There are exceptions,
 * though. Unfortunately IRQ probing is not possible without touching
 * the device attached to the port.
 */

#define I8042_AUX_IRQ CONFIG_I8042_AUX_IRQ

/*
 * This is in 50us units, the time we wait for the i8042 to react. This
 * has to be long enough for the i8042 itself to timeout on sending a byte
 * to a non-existent mouse.
 */

#define I8042_CTL_TIMEOUT	10000

/*
 * When the device isn't opened and it's interrupts aren't used, we poll it at
 * regular intervals to see if any characters arrived. If yes, we can start
 * probing for any mouse / keyboard connected. This is the period of the
 * polling.
 */

#define I8042_POLL_PERIOD	HZ/20

/*
 * Register numbers.
 */

#define I8042_COMMAND_REG	CONFIG_I8042_REG_BASE + 4	
#define I8042_STATUS_REG	CONFIG_I8042_REG_BASE + 4	
#define I8042_DATA_REG		CONFIG_I8042_REG_BASE	

/*
 * Status register bits.
 */

#define I8042_STR_PARITY	0x80
#define I8042_STR_TIMEOUT	0x40
#define I8042_STR_AUXDATA	0x20
#define I8042_STR_KEYLOCK	0x10
#define I8042_STR_CMDDAT	0x08
#define I8042_STR_IBF		0x02
#define	I8042_STR_OBF		0x01

/*
 * Control register bits.
 */

#define I8042_CTR_KBDINT	0x01
#define I8042_CTR_AUXINT	0x02
#define I8042_CTR_IGNKEYLOCK	0x08
#define I8042_CTR_KBDDIS	0x10
#define I8042_CTR_AUXDIS	0x20
#define I8042_CTR_XLATE		0x40

/*
 * Commands.
 */

#define I8042_CMD_CTL_RCTR	0x0120
#define I8042_CMD_CTL_WCTR	0x1060
#define I8042_CMD_CTL_TEST	0x01aa

#define I8042_CMD_KBD_DISABLE	0x00ad
#define I8042_CMD_KBD_ENABLE	0x00ae
#define I8042_CMD_KBD_TEST	0x01ab
#define I8042_CMD_KBD_LOOP	0x11d2

#define I8042_CMD_AUX_DISABLE	0x00a7
#define I8042_CMD_AUX_ENABLE	0x00a8
#define I8042_CMD_AUX_TEST	0x01a9
#define I8042_CMD_AUX_SEND	0x10d4
#define I8042_CMD_AUX_LOOP	0x11d3

/*
 * Return codes.
 */

#define I8042_RET_CTL_TEST	0x55

/*
 * Expected maximum internal i8042 buffer size. This is used for flushing
 * the i8042 buffers. 32 should be more than enough.
 */

#define I8042_BUFFER_SIZE	32

#endif /* _I8042_H */

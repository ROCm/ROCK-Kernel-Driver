/*
 *  linux/drivers/acorn/char/serial-dualsp.c
 *
 *  Copyright (C) 1996 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   30-07-1996	RMK	Created
 */
#define MY_CARD_LIST { MANU_SERPORT, PROD_SERPORT_DSPORT }
#define MY_NUMPORTS 2
#define MY_BAUD_BASE (3686400 / 16)
#define MY_BASE_ADDRESS(ec) \
	ecard_address (ec, ECARD_IOC, ECARD_SLOW) + (0x2000 >> 2)
#define MY_PORT_ADDRESS(port,cardaddress) \
	((cardaddress) + (port) * 8)

#define INIT serial_card_dualsp_init
#define EXIT serial_card_dualsp_exit

#include "serial-card.c"

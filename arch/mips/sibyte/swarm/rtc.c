/*
 * Copyright (C) 2000, 2001 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 *  Not really sure what is supposed to be here, yet
 */

#include <linux/spinlock.h>
#include <linux/mc146818rtc.h>

static unsigned char swarm_rtc_read_data(unsigned long addr)
{
	return 0;
}

static void swarm_rtc_write_data(unsigned char data, unsigned long addr)
{
}

static int swarm_rtc_bcd_mode(void)
{
	return 0;
}

struct rtc_ops swarm_rtc_ops = {
	&swarm_rtc_read_data,
	&swarm_rtc_write_data,
	&swarm_rtc_bcd_mode
};

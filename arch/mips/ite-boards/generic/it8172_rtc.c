/*
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * RTC routines for ITE8172 MC146818-compatible rtc chip.
 *
 */
#include <linux/mc146818rtc.h>
#include <asm/it8172/it8172.h>

#define IT8172_RTC_ADR_REG  (IT8172_PCI_IO_BASE + IT_RTC_BASE)
#define IT8172_RTC_DAT_REG  (IT8172_RTC_ADR_REG + 1)

static volatile char *rtc_adr_reg = KSEG1ADDR((volatile char *)IT8172_RTC_ADR_REG);
static volatile char *rtc_dat_reg = KSEG1ADDR((volatile char *)IT8172_RTC_DAT_REG);

unsigned char it8172_rtc_read_data(unsigned long addr)
{
	unsigned char retval;

	*rtc_adr_reg = addr;
	retval =  *rtc_dat_reg;
	return retval;
}

void it8172_rtc_write_data(unsigned char data, unsigned long addr)
{
	*rtc_adr_reg = addr;
	*rtc_dat_reg = data;
}

static int it8172_rtc_bcd_mode(void)
{
	return 0;
}

struct rtc_ops it8172_rtc_ops = {
	&it8172_rtc_read_data,
	&it8172_rtc_write_data,
	&it8172_rtc_bcd_mode
};

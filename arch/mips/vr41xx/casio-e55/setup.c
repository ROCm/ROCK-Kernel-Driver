/*
 *  setup.c, Setup for the CASIO CASSIOPEIA E-11/15/55/65.
 *
 *  Copyright (C) 2002-2003  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <linux/root_dev.h>

#include <asm/time.h>
#include <asm/vr41xx/e55.h>

#ifdef CONFIG_BLK_DEV_INITRD
extern unsigned long initrd_start, initrd_end;
extern void * __rd_start, * __rd_end;
#endif

static void __init casio_e55_setup(void)
{
	set_io_port_base(IO_PORT_BASE);
	ioport_resource.start = IO_PORT_RESOURCE_START;
	ioport_resource.end = IO_PORT_RESOURCE_END;
	iomem_resource.start = IO_MEM_RESOURCE_START;
	iomem_resource.end = IO_MEM_RESOURCE_END;

#ifdef CONFIG_BLK_DEV_INITRD
	ROOT_DEV = Root_RAM0;
	initrd_start = (unsigned long)&__rd_start;
	initrd_end = (unsigned long)&__rd_end;
#endif

	board_time_init = vr41xx_time_init;
	board_timer_setup = vr41xx_timer_setup;

	vr41xx_bcu_init();

	vr41xx_cmu_init();

	vr41xx_pmu_init();

#ifdef CONFIG_SERIAL_8250
	vr41xx_siu_init(SIU_RS232C, 0);
#endif
}

early_initcall(casio_e55_setup);

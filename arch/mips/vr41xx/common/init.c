/*
 *  init.c, Common initialization routines for NEC VR4100 series.
 *
 *  Copyright (C) 2003-2004  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
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
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/vr41xx/vr41xx.h>

extern void vr41xx_bcu_init(void);
extern void vr41xx_cmu_init(void);
extern void vr41xx_pmu_init(void);
extern void vr41xx_rtc_init(void);

void __init prom_init(void)
{
	int argc, i;
	char **argv;

	argc = fw_arg0;
	argv = (char **)fw_arg1;

	for (i = 1; i < argc; i++) {
		strcat(arcs_cmdline, argv[i]);
		if (i < (argc - 1))
			strcat(arcs_cmdline, " ");
	}

	iomem_resource.start = IO_MEM_RESOURCE_START;
	iomem_resource.end = IO_MEM_RESOURCE_END;

	vr41xx_bcu_init();
	vr41xx_cmu_init();
	vr41xx_pmu_init();
	vr41xx_rtc_init();
}

unsigned long __init prom_free_prom_memory (void)
{
	return 0UL;
}

/*
 * pmc.c
 * Copyright (C) 2001 Dave Engebretsen & Mike Corrigan IBM Corporation
 * 
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/* Change Activity:
 * 2001/06/05 : engebret : Created.
 * End Change Activity 
 */

#include <asm/proc_fs.h>
#include <asm/paca.h>
#include <asm/iSeries/ItLpPaca.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/processor.h>

#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <asm/pmc.h>
#include <asm/uaccess.h>
#include <asm/naca.h>

struct _pmc_sw pmc_sw_system = {
	0  
};

struct _pmc_sw pmc_sw_cpu[NR_CPUS] = {
	{0 }, 
};

/*
 * Provide enough storage for either system level counters or
 * one cpu's counters.
 */
struct _pmc_sw_text pmc_sw_text;
struct _pmc_hw_text pmc_hw_text;

char *
ppc64_pmc_stab(int file)
{
	int  n;
	unsigned long stab_faults, stab_capacity_castouts, stab_invalidations; 
	unsigned long i;

	stab_faults = stab_capacity_castouts = stab_invalidations = n = 0;

	if (file == -1) {
		for (i = 0;  i < NR_CPUS; i++) {
			if (!cpu_online(i))
				continue;
			stab_faults += pmc_sw_cpu[i].stab_faults;
			stab_capacity_castouts += pmc_sw_cpu[i].stab_capacity_castouts;
			stab_invalidations += pmc_sw_cpu[i].stab_invalidations;
		}
		n += sprintf(pmc_sw_text.buffer + n,    
			     "Faults         0x%lx\n", stab_faults); 
		n += sprintf(pmc_sw_text.buffer + n, 
			     "Castouts       0x%lx\n", stab_capacity_castouts); 
		n += sprintf(pmc_sw_text.buffer + n, 
			     "Invalidations  0x%lx\n", stab_invalidations); 
	} else {
		n += sprintf(pmc_sw_text.buffer + n,
			     "Faults         0x%lx\n", 
			     pmc_sw_cpu[file].stab_faults);
		
		n += sprintf(pmc_sw_text.buffer + n,   
			     "Castouts       0x%lx\n", 
			     pmc_sw_cpu[file].stab_capacity_castouts);
		
		n += sprintf(pmc_sw_text.buffer + n,   
			     "Invalidations  0x%lx\n", 
			     pmc_sw_cpu[file].stab_invalidations);

		for (i = 0; i < STAB_ENTRY_MAX; i++) {
			if (pmc_sw_cpu[file].stab_entry_use[i]) {
				n += sprintf(pmc_sw_text.buffer + n,   
					     "Entry %02ld       0x%lx\n", i, 
					     pmc_sw_cpu[file].stab_entry_use[i]);
			}
		}

	}

	return(pmc_sw_text.buffer); 
}

char *
ppc64_pmc_htab(int file)
{
	int  n;
	unsigned long htab_primary_overflows, htab_capacity_castouts;
	unsigned long htab_read_to_write_faults; 

	htab_primary_overflows = htab_capacity_castouts = 0;
	htab_read_to_write_faults = n = 0;

	if (file == -1) {
		n += sprintf(pmc_sw_text.buffer + n,    
			     "Primary Overflows  0x%lx\n", 
			     pmc_sw_system.htab_primary_overflows); 
		n += sprintf(pmc_sw_text.buffer + n, 
			     "Castouts           0x%lx\n", 
			     pmc_sw_system.htab_capacity_castouts); 
	} else {
		n += sprintf(pmc_sw_text.buffer + n,
			     "Primary Overflows  N/A\n");

		n += sprintf(pmc_sw_text.buffer + n,   
			     "Castouts           N/A\n\n");

	}
	
	return(pmc_sw_text.buffer); 
}

char *
ppc64_pmc_hw(int file)
{
	int  n;

	n = 0;
	if (file == -1) {
		n += sprintf(pmc_hw_text.buffer + n, "Not Implemented\n");
	} else {
		n += sprintf(pmc_hw_text.buffer + n,    
			     "MMCR0  0x%lx\n", mfspr(MMCR0)); 
		n += sprintf(pmc_hw_text.buffer + n, 
			     "MMCR1  0x%lx\n", mfspr(MMCR1)); 
#if 0
		n += sprintf(pmc_hw_text.buffer + n,    
			     "MMCRA  0x%lx\n", mfspr(MMCRA)); 
#endif

		n += sprintf(pmc_hw_text.buffer + n,    
			     "PMC1   0x%lx\n", mfspr(PMC1)); 
		n += sprintf(pmc_hw_text.buffer + n,    
			     "PMC2   0x%lx\n", mfspr(PMC2)); 
		n += sprintf(pmc_hw_text.buffer + n,    
			     "PMC3   0x%lx\n", mfspr(PMC3)); 
		n += sprintf(pmc_hw_text.buffer + n,    
			     "PMC4   0x%lx\n", mfspr(PMC4)); 
		n += sprintf(pmc_hw_text.buffer + n,    
			     "PMC5   0x%lx\n", mfspr(PMC5)); 
		n += sprintf(pmc_hw_text.buffer + n,    
			     "PMC6   0x%lx\n", mfspr(PMC6)); 
		n += sprintf(pmc_hw_text.buffer + n,    
			     "PMC7   0x%lx\n", mfspr(PMC7)); 
		n += sprintf(pmc_hw_text.buffer + n,    
			     "PMC8   0x%lx\n", mfspr(PMC8)); 
	}

	return(pmc_hw_text.buffer); 
}

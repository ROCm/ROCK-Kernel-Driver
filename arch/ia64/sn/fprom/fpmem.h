/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 */



#include <asm/sn/mmzone_sn1.h>

typedef struct sn_memmap_s
{
	short		nasid ;
	short		cpuconfig;
	node_memmap_t 	node_memmap ;
} sn_memmap_t ;

typedef struct sn_config_s
{
	int		cpus;
	int		nodes;
	sn_memmap_t	memmap[1];		/* start of array */
} sn_config_t;


extern void build_init(unsigned long);
extern int build_efi_memmap(void *, int);
extern int GetNumNodes(void);
extern int GetNumCpus(void);
extern int IsCpuPresent(int, int);
extern int GetNasid(int);

/*
 *  vtoshooks.h
 *
 *  Copyright (C) 2002-2004 Intel Corporation
 *  Maintainer - Juan Villacis <juan.villacis@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
/*
 * ===========================================================================
 *
 *	File: vtoshooks.h
 *
 *	Description: common header file used by vtoshooks*.c
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#if !defined(_VTUNE_OSHOOKS_H)
#define _VTUNE_OSHOOKS_H

#include "vtypes.h"

extern __u32 track_module_loads;	// track module loads
extern __u32 track_process_creates;	// track module loads

typedef struct _MGID_INFO	// Module Group ID information
{
	__u32 mgid;		// module group id
	__u32 reserved;		// reserved
	void_ptr mr_first;	// address of first module record in the module group (may be in any module record buffer)
	void_ptr *mr_last;	// address of last module record in the module group (may be in any module record buffer)
} MGID_INFO, *PMGID_INFO;

void alloc_module_group_ID(PMGID_INFO pmgid_info);

void update_pid_for_module_group(__u32 pid, PMGID_INFO pmgid_info);

int samp_load_image_notify_routine(char *name, __u32_PTR base,__u32_PTR size,
				   __u32 pid, __u32 options, PMGID_INFO pmgid_info,
				   unsigned short  mode);

unsigned short get_exec_mode(struct task_struct *p);

// defines for options paramater of samp_load_image_notify_routine(...)
#define LOPTS_1ST_MODREC    0x1
#define LOPTS_GLOBAL_MODULE 0x2

void samp_create_process_notify_routine(__u32 parent_id, __u32 process_id, __u32 create);

#ifndef EXPORTED_SYS_CALL_TABLE
void *find_sys_call_table_symbol(int verbose);
#endif

#ifdef EXPORTED_FOR_EACH_PROCESS
#define for_each_task for_each_process
#endif

#endif /* _VTUNE_OSHOOKS_H */

/*
 * Platform dependent support for IO probing.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All rights reserved.
 */

#include <asm/sn/sgi.h>
#include <asm/sn/sn_sal.h>

/**
 * ia64_sn_probe_io_slot - test a memory location for readability
 * @paddr: physical address to probe
 * @size: number bytes to read (1,2,4,8)
 * @data_ptr: address to store value read by probe (-1 returned if probe fails)
 *
 * This function will probe a physical address to determine if
 * the address can be read. If reading the address causes a BUS
 * error, an error is returned. If the probe succeeds, the contents 
 * of the memory location is returned.
 *
 * Return values:
 *  0 - probe successful
 *  1 - probe failed (generated MCA)
 *  2 - Bad arg
 * <0 - PAL error
 */
u64
ia64_sn_probe_io_slot(long paddr, long size, void *data_ptr)
{
	struct ia64_sal_retval isrv;

	SAL_CALL(isrv, SN_SAL_PROBE, paddr, size, 0, 0, 0, 0, 0);

	if (data_ptr) {
		switch (size) {
			case 1:
				*((u8*)data_ptr) = (u8)isrv.v0;
				break;
			case 2:
				*((u16*)data_ptr) = (u16)isrv.v0;
				break;
			case 4:
				*((u32*)data_ptr) = (u32)isrv.v0;
				break;
			case 8:
				*((u64*)data_ptr) = (u64)isrv.v0;
				break;
			default:
				isrv.status = 2;	
		}
	}

	return isrv.status;
}

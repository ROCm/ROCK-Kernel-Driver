/*
 *  apic.h
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
 *	File: apic.h
 *
 *	Description: header file for apic on IA32 platforms 
 *
 *	Author(s): Charles Spirakis, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#if !defined(__VTUNE_APIC_H_)
#define __VTUNE_APIC_H_

//
// How big is the apic area that is mapped into physical memory
//
#define APIC_PHYSICAL_LENGTH    4096

//
// The feature flags from CPUID instruction...
//
#define CPUID_EDX_HASAPIC     (1 << 9)


//
// APIC related MSRs
//

#define IA32_APIC_BASE      0x1b

//
// And ia32_apic_base bit info
//
#define APIC_BASE_BSP               (1 << 8)
#define APIC_BASE_GLOBAL_ENABLE     (1 << 11)

//
//
// The linux apic headers files handle the read and make sure
// the reads go to the correct area of memory. So, the base is
// unused in this case...
//
#define APIC_READ(base, offset)         (apic_read(offset))
#define APIC_WRITE(base, offset, value) (apic_write(offset, value))

//
// Priority levels for use in the TASKPRI register
#define APIC_VALUE_TPR_HI       0xf0
#define APIC_VALUE_TPR_LO       0x00

//
// at least try to be nice...
//
#ifndef PERF_MON_VECTOR
#define PERF_MON_VECTOR 0xF0
#endif

//
// Routines that can be called that are apic related...
//
void EnablePerfVect(
        __u32 wantEnable
        );

__u32
SetApicPerfLevel(
        __u32 newValue
        );

void
SetVirtualWireMode(
        void
        );
__u32
IsApicEnabled(
        void
        );

void DoApicInit(
        void
        );

#endif // __VTUNE_APIC_H_

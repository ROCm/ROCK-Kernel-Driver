/*
 *  vtasm32.h
 *
 *  Copyright (C) 2003-2004 Intel Corporation
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
 *	File: vtasm32.h
 *
 *	Description: type definitions for IA32 based platforms
 *
 *	Author(s): Charles Spirakis, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#if !defined(__VTUNE_ASM32_H__)
#define __VTUNE_ASM32_H__

//
// Define the various HW structures. To do that, 
// we need to avoid having the compiler add padding...
//
#pragma pack(push, 1)
//
// For the GDTR and IDTR regsiters
//
typedef struct _idtgdtDesc {
    __u16 idtgdt_limit;
    void *idtgdt_base;
} IDTGDT_DESC;

//
// A "long" IDT entry...
//
typedef struct _IdtEntry {
    union {
        __u64 lowQuad;
        struct {
            __u64 offsetLow : 16;     // Offset 15:00
            __u64 selector : 16;      // segment selector value

            __u64 ist : 3;            // interrupt stack switch
            __u64 reserved0 : 5;      // reserved/ignored
            __u64 type : 4;           // Type should == 0xe for int descriptor
            __u64 sysUsr: 1;          // system/user should == 0 for int descriptor
            __u64 dpl : 2;            // Dpl
            __u64 pres : 1;           // present bit
            __u64 offsetMid : 16;     // offset 31:16
        };
    };
} IDT_ENTRY;

/* NOTE:  also defined in include/asm-x86_64/desc.h */
struct gate_struct {
    u16 a;
    u16 b;
    u16 c;
    u16 d;
} ;
#pragma pack(pop)

#define GDT_SEGMENT_SHIFT       3

//
// These functions were all written in assembly and make an assumption
// regarding the calling convention... Spell out that assumption...
//

void ReadTsc(__u64 *pTsc);

void GetIDTDesc(IDTGDT_DESC *pIdtDesc);
void GetGDTDesc(IDTGDT_DESC *pGdtDesc);

void ReadMsr(__u32 msr, __u64 *pResult);
void WriteMsr(__u32 msr, __u64 value);

__u8 __inbyte(__u32 port);
void __outbyte(__u32 port, __u8 val);

#endif // __VTUNE_ASM32_H__

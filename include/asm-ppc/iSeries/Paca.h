/*
 * Paca.h
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
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


//============================================================================
//
// This control block defines the OS's PACA which defines the processor 
// specific data for each logical processor on the system.  
// There are some pointers defined that are utilized by PLIC.
//   

#ifndef	_PPC_TYPES_H
#include	<asm/types.h>
#endif

//-----------------------------------------------------------------------------
// Other Includes
//-----------------------------------------------------------------------------
#ifndef	_ITLPPACA_H
#include	<asm/iSeries/ItLpPaca.h>
#endif

#ifndef	_ITLPREGSAVE_H
#include	<asm/iSeries/ItLpRegSave.h>
#endif

#ifndef _ITLPQUEUE_H
#include	<asm/iSeries/ItLpQueue.h>
#endif


#ifndef _PACA_H
#define _PACA_H

/* 
 * The bolted stack structure is at the head of each bolted stack
 * and is simply a singly linked list
 */

//============================================================================
//
//	Defines the layout of the Paca.  
//
//	This structure is not directly accessed by PLIC or the SP except
//	for the first two pointers that point to the ItLpPaca area and the
//	ItLpRegSave area for this processor.
//
//============================================================================
struct Paca
{
//===========================================================================
// Following two fields are read by PLIC to find the LpPaca and LpRegSave area
//===========================================================================

  u32		pad1;			// Pointer to LpPaca for proc		
  struct ItLpPaca * xLpPacaPtr;		// Pointer to LpPaca for proc
  u32		pad2;			// Pointer to LpRegSave for proc
  struct ItLpRegSave * xLpRegSavePtr;	// Pointer to LpRegSave for proc	

  u64		xR21;			// Savearea for GPR21
  u64		xR22;			// Savearea for GPR22
  u64		xKsave;			// Saved Kernel stack addr or zero
  
  u16		xPacaIndex;		// Index into Paca array of this
  					// Paca.  This is processor number
  u8		xProcStart;		// At startup, processor spins until
  					// xProcStart becomes non-zero.
  u8		xProcEnabled;		// 1 - soft enabled, 0 - soft disabled
  u32           xrsvd2;             	// was bolted stacks   
  u32		xSavedMsr;		// old msr saved here by HvCall
  					// and flush_hash_page.
  					// HvCall uses 64-bit registers
					// so it must disable external
					// interrupts to avoid the high
					// half of the regs getting lost
					// It can't stack a frame because
					// some of the callers can't 
					// tolerate hpt faults (which might
					// occur on the stack)
  u32		xSavedLr;		// link register saved here by
  					// flush_hash_page
  u8		xContextOverflow;	// 1 - context overflow - use temporary
  					// context = processor# + 1
  u8		rsvd4;
  u16		rsvd5;
  u32		xSRR0;			// Used as bolted copies of stack fields
  u32		xSRR1;
  u32		xGPR0;
  u32		xGPR2;
  u32		default_decr;		// Default decrementer value
  u32		ext_ints;		// ext ints processed 
  u32		rsvd6;
  u64		rsvd1[5];		// Rest of cache line reserved

//===========================================================================
// CACHE_LINE_2-3 0x0080 - 0x0180
//===========================================================================

  struct ItLpQueue * lpQueuePtr;	// LpQueue handled by this processor
  u32		breakpoint_loop;	// Loop until this field is set
  					// non-zero by user.  Then set it
					// back to zero before continuing

  u64		debug_regs;		// Pointer to pt_regs at breakpoint
  u64		rsvd3[30];		// To be used by Linux

//===========================================================================
// CACHE_LINE_4-8  0x0180 - 0x03FF Contains ItLpPaca
//===========================================================================

  struct ItLpPaca xLpPaca;	// Space for ItLpPaca		

//===========================================================================
// CACHE_LINE_9-16 0x0400 - 0x07FF Contains ItLpRegSave
//===========================================================================

  struct ItLpRegSave xRegSav;	// Register save for proc	

};

#endif /* _PACA_H */

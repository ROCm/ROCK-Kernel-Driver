/*
 * IoHriMainStore.h
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

#ifndef _IOHRIMAINSTORE_H
#define _IOHRIMAINSTORE_H

struct IoHriMainStoreSegment4 {    
  u8	msArea0Exists:1;
  u8	msArea1Exists:1;
  u8	msArea2Exists:1;
  u8	msArea3Exists:1;
  u8	reserved1:4;
  u8	reserved2;

  u8	msArea0Functional:1;
  u8	msArea1Functional:1;
  u8	msArea2Functional:1;
  u8	msArea3Functional:1;
  u8	reserved3:4;
  u8	reserved4;

  u32	totalMainStore;

  u64	msArea0Ptr;
  u64	msArea1Ptr;
  u64	msArea2Ptr;
  u64	msArea3Ptr;

  u32	cardProductionLevel;

  u32	msAdrHole;

  u8	msArea0HasRiserVpd:1;
  u8	msArea1HasRiserVpd:1;
  u8	msArea2HasRiserVpd:1;
  u8	msArea3HasRiserVpd:1;
  u8	reserved5:4;	
  u8	reserved6;
  u16	reserved7;

  u8	reserved8[28];

  u64	nonInterleavedBlocksStartAdr;
  u64	nonInterleavedBlocksEndAdr;
};


#endif // _IOHRIMAINSTORE_H


/*
 * HvLpEvent.c
 * Copyright (C) 2001 Mike Corrigan  IBM Corporation
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

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvCallEvent.h>
#include <asm/iSeries/LparData.h>

// Array of LpEvent handler functions
LpEventHandler lpEventHandler[HvLpEvent_Type_NumTypes];
unsigned lpEventHandlerPaths[HvLpEvent_Type_NumTypes];

// Register a handler for an LpEvent type

int HvLpEvent_registerHandler( HvLpEvent_Type eventType, LpEventHandler handler )
{
	int rc = 1;
	if ( eventType < HvLpEvent_Type_NumTypes ) {
		lpEventHandler[eventType] = handler;
		rc = 0;
	}
	return rc;
	
}

int HvLpEvent_unregisterHandler( HvLpEvent_Type eventType )
{
	int rc = 1;
	if ( eventType < HvLpEvent_Type_NumTypes ) {
		if ( !lpEventHandlerPaths[eventType] ) {
			lpEventHandler[eventType] = NULL;
			rc = 0;
		}
	}
	return rc;
}

// (lpIndex is the partition index of the target partition.  
// needed only for VirtualIo, VirtualLan and SessionMgr.  Zero
// indicates to use our partition index - for the other types)
int HvLpEvent_openPath( HvLpEvent_Type eventType, HvLpIndex lpIndex )
{
	int rc = 1;
	if ( eventType < HvLpEvent_Type_NumTypes &&
	     lpEventHandler[eventType] ) {
		if ( lpIndex == 0 )
			lpIndex = itLpNaca.xLpIndex;
		HvCallEvent_openLpEventPath( lpIndex, eventType );
		++lpEventHandlerPaths[eventType];
		rc = 0;
	}
	return rc;
}

int HvLpEvent_closePath( HvLpEvent_Type eventType, HvLpIndex lpIndex )
{
	int rc = 1;
	if ( eventType < HvLpEvent_Type_NumTypes &&
	     lpEventHandler[eventType] &&
	     lpEventHandlerPaths[eventType] ) {
		if ( lpIndex == 0 )
			lpIndex = itLpNaca.xLpIndex;
		HvCallEvent_closeLpEventPath( lpIndex, eventType );
		--lpEventHandlerPaths[eventType];
		rc = 0;
	}
	return rc;
}


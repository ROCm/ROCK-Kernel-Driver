/* 
 *   Creation Date: <2002/01/17 20:50:22 samuel>
 *   Time-stamp: <2003/08/12 00:40:58 samuel>
 *   
 *	<actions.S>
 *	
 *	Action symbols
 *   
 *   Copyright (C) 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

/* Action symbols are used to trigger specific actions. For instance, 
 *
 *	.long Action_LI_PHYS + N
 *	.long label
 * ...
 *	label:	
 *
 * Is replaced with
 *	li	rN,physaddr@l
 *	addis	rN,rN,physaddr@ha
 *
 * where physaddr is the address of the code at 'label'
 * (determined at runtime).
 */

#include "archinclude.h"
#include "asmdefs.h"
#include "reloc.h"

	.data
	ACTION_SYMBOL( Action_LI_PHYS, ACTION_LI_PHYS )
	ACTION_SYMBOL( Action_LWZ_PHYSADDR_R, ACTION_LWZ_PHYSADDR_R )
	ACTION_SYMBOL( Action_TOPHYS, ACTION_TOPHYS )
	ACTION_SYMBOL( Action_TOVIRT, ACTION_TOVIRT )
	ACTION_SYMBOL( Symbol_SESSION_TABLE, ACTION_SESSION_TABLE )
	ACTION_SYMBOL( Action_RELOC_HOOK, ACTION_RELOC_HOOK )
	ACTION_SYMBOL( Action_VRET, ACTION_VRET )
	ACTION_SYMBOL( Action_HOOK_FUNCTION, ACTION_HOOK_FUNCTION )
	ACTION_SYMBOL( Action_RELOCATE_LOW, ACTION_RELOCATE_LOW )

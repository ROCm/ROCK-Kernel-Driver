/* 
 *   Creation Date: <1999/09/25 17:25:30 samuel>
 *   Time-stamp: <2003/08/12 00:40:40 samuel>
 *   
 *	<reloc.h>
 *	
 *	Definitions used to move criticalal low-level parts
 *	of the code to continuous physical memory.
 *   
 *   Copyright (C) 1999, 2000, 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_RELOC
#define _H_RELOC

/* Special 'action symbols' are used to dynamically modify the assembly code. 
 * The script relbuild.pl is used to store information about relocations
 * and special actions in a C-table.
 *
 * Reloc actions are performed when the code is relocated at module
 * initialization. Hook actions are performed when the first session
 * is initialized. The remaining actions are used to resolve addresses
 * of dynamic symbols.
 */

#define NO_ACTION			0
#define  ACTION_SESSION_TABLE		1

#define FIRST_RELOC_ACTION		4
#define   ACTION_LI_PHYS		5	/* dest_reg, addr_offs */
#define   ACTION_LWZ_PHYSADDR_R		6	/* dest_reg*32 + reg, addr_offs */
#define   ACTION_TOPHYS			7	/* dest_reg*32 + src_reg */
#define   ACTION_TOVIRT			8	/* dest_reg*32 + src_reg */
#define LAST_RELOC_ACTION		8

#define FIRST_HOOK_ACTION		10
#define   ACTION_RELOC_HOOK		11	/* trigger, size, vret_action#, vret_offs */
#define   ACTION_VRET			12
#define   ACTION_HOOK_FUNCTION		13
#define   ACTION_RELOCATE_LOW		14	/* not a hook but it fits here... */
#define LAST_HOOK_ACTION		14

#define MAX_ACTION			15	/* used to loop the actions */

/* Function hooks */
#define FHOOK_FLUSH_HASH_PAGE	1


#ifdef ACTION_SYM_GREPPING
#define ACTION_SYMBOL( sym_name, action ) \
	ACTION_SYM_START,##sym_name,##action,ACTION_SYM_END
#define GLOBAL_SYMBOL( sym_name ) \
	GLOBAL_SYM_START,##sym_name,GLOBAL_SYM_END
#else
#define ACTION_SYMBOL( sym_name, dummy2 ) \
GLOBL(sym_name):
#define GLOBAL_SYMBOL( sym_name ) \
GLOBL(sym_name)
#endif


#ifndef __ASSEMBLY__

extern int reloc_virt_offs;
#define reloc_ptr( v )  ((ulong)(v) + (ulong)reloc_virt_offs)

enum {
	MOL_R_PPC_ADDR16_LO = 1,
	MOL_R_PPC_ADDR16_HA,
	MOL_R_PPC_ADDR16_HI,
	MOL_R_PPC_ADDR32
	/* unsupported */
	/* MOL_R_PPC_REL24, */
	/* MOL_R_PPC_REL14, */
};

typedef struct reloc_table {
	unsigned long 	inst_offs;
	int 		action;
	int		rtype;
	unsigned long	offs;
} reloc_table_t, reloc_table_entry_t;
#endif

#endif   /* _H_RELOC */

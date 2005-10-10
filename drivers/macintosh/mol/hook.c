/* 
 *   Creation Date: <2001/04/07 17:33:52 samuel>
 *   Time-stamp: <2003/08/26 21:40:53 samuel>
 *   
 *	<hook.c>
 *	
 *	Relocates the assembly code and inserts kernel hooks
 *   
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#include "archinclude.h"
#include "alloc.h"
#include "reloc.h"
#include "reloc_table.h"
#include "kernel_vars.h"
#include "misc.h"
#include "asmfuncs.h"


/* exports from base.S */
extern char 	r__reloctable_start[], r__reloctable_end[];
extern char 	r__flush_hash_page_hook[];

/* globals */
int		reloc_virt_offs;

static char 	*code_base;
static int	hooks_written;

static ulong	*hash_hook_handle;


/************************************************************************/
/*	Code Relocation							*/
/************************************************************************/

static void
do_reloc_action( int act, ulong *target, const ulong *pb )
{
	ulong addr = pb[1] + tophys_mol( code_base );

	/* LI_PHYS - insert li r, phys */
	if( act == ACTION_LI_PHYS ) {
		int r = pb[0] & 0xf;
		/* target[0] = addis r,0,addr@h ; target[1] = oris r,r,addr@l */
		target[0] = (15 << 26) | (r << 21) | (addr >> 16);	
		target[1] = (24 << 26) | (r << 21) | (r << 16) | (addr & 0xffff);
		/* printk("ACTION_LI_PHYS %d %08lX\n", dreg, addr ); */
		return;
	}
	/* LWZ_PHYS_R - insert lwz dr,phys(r) */
	if( act == ACTION_LWZ_PHYSADDR_R ) {
		int dr = (pb[0] >> 5) & 0xf;
		int r = pb[0] & 0xf;
		short low = (addr & 0xffff);
		
		/* target[0] = addis dr,r,addr@h ; target[1] = lwz dr,addr@l(dr) */
		target[0] = (15 << 26) | (dr << 21) | (r << 16) | ((addr - low) >> 16);
		target[1] = (32 << 26) | (dr << 21) | (dr << 16) | ((int)low & 0xffff);
		/* printk("ACTION_LI_PHYS %d %08lX\n", dreg, addr ); */
		return;
	}
	/* TOPHYS -- insert addis dr,sr,physaddr(0) */
	if( act == ACTION_TOPHYS || act == ACTION_TOVIRT ) {
		int dr = (pb[0] >> 5) & 0xf;
		int sr = pb[0] & 0xf;
		addr = tophys_mol(0);
		if( act == ACTION_TOVIRT )
			addr = -addr;
		/* target[0] = addis dr,sr,(tophys(0))@ha */
		target[0] = (15 << 26) | (dr << 21) | (sr << 16) | (addr >> 16);
		return;
	}
	printk("Unimplemented reloc action %d\n", act );
}

int 
relocate_code( void )
{
	ulong code_size;
	int i, action;

	code_size = r__reloctable_end - r__reloctable_start;
	if( !(code_base=kmalloc_cont_mol(code_size)) )
		return 1;

	memcpy( code_base, r__reloctable_start, code_size );

	reloc_virt_offs = (int)code_base - (int)r__reloctable_start;

	/* relocate the code */
	for( i=0; (action=reloc_table[i].action) >= 0 ; i++ ) {
		reloc_table_entry_t *re = &reloc_table[i];
		ulong addr = (ulong)code_base;
		ulong *target;

		/* actions */
		if( action >= FIRST_HOOK_ACTION && action <= LAST_HOOK_ACTION )
			continue;

		if( action >= FIRST_RELOC_ACTION && action <= LAST_RELOC_ACTION ) {
			ulong *pb = (ulong*)&r__reloctable_start[re->inst_offs] + 1;
			target = (ulong*)(code_base + re->offs);

			if( re->offs >= code_size )
				return 1;
			do_reloc_action( action, target, pb );
			continue;
		}

		/* dynamic symbols */
		switch( action ) {
		case ACTION_SESSION_TABLE:
			addr = tophys_mol(g_sesstab);
			break;
		case NO_ACTION:
			/* local symbol addressed absolutely */
			break;
		default:
			printk("Unimplemented Action %d\n", action );
			break;
		}

		/* ignore stuff which is not relocated */
		if( re->inst_offs >= code_size )
			continue;
		target = (ulong*)(code_base + re->inst_offs);

		addr += re->offs;
		switch( re->rtype ) {
		case MOL_R_PPC_ADDR32:
			*target = addr;
			break;
		case MOL_R_PPC_ADDR16_HA:
			*(ushort*)target = (addr - (long)((short)addr)) >> 16;
			break;
		case MOL_R_PPC_ADDR16_HI:
			*(ushort*)target = (addr >> 16);
			break;
		case MOL_R_PPC_ADDR16_LO:
			*(ushort*)target = addr & 0xffff;
			break;
		/* case MOL_R_PPC_REL14: */
		/* case MOL_R_PPC_REL24: */
		default:
			printk("Relocation type %d unsupported\n", re->rtype );
			relocation_cleanup();
			return 1;
		}
	}
	flush_icache_mol( (ulong)code_base, (ulong)code_base + code_size );
	
	/* printk("Code relocated, %08lX -> %08lX (size %08lX)\n", r__reloctable_start, code_base, code_size ); */
	return 0;
}

void
relocation_cleanup( void ) 
{
	if( code_base ) {
		kfree_cont_mol( code_base );
		code_base = NULL;
	}
}


/************************************************************************/
/*	LowMem Allocation (allocates within the first 32 MB of RAM)	*/
/************************************************************************/

/* The low-level assembly code need to be located in memory which is
 * physically continuous. The kernel exception vector are patched
 * through pseudo symbols (action symbols).
 */

#define MAX_NUM_CLEANUP_HANDLERS	32

typedef struct {
	char	*lowmem_addr;
	int	alloc_size;
	int	alloc_method;

	ulong	*inst_addr;			/* these fields are used */
	ulong	opcode;				/* be the hook code */
} cleanup_entry_t;

static int 		num_cleanup_entries;
static cleanup_entry_t 	cleanup_table[ MAX_NUM_CLEANUP_HANDLERS ];
static ulong		lowmem_phys_cursor;

/* Memory mapping of exception vectors */
static ulong		lowmem_phys_base;
static char		*lowmem_virt_base;
static void		*lowmem_mapping;

static inline ulong *
lowmem_phys_to_virt( ulong paddr ) {
	return (ulong*)(lowmem_virt_base + (paddr - lowmem_phys_base));
}

static inline ulong
lowmem_tophys( void *vaddr ) {
	return lowmem_phys_base + ((ulong)vaddr - (ulong)lowmem_virt_base);
}


static void 
lowmem_initialize( void )
{
	if( num_cleanup_entries ) {
		printk("Internal error in lowmem_initialize\n");
		return;
	}
	lowmem_phys_cursor = 0x100;

	/* In Darwin, the mapping will fail if we put lowmem_phys_base to zero */
	lowmem_phys_base = 0x100;
	lowmem_mapping = map_phys_range( lowmem_phys_base, 0x4000, &lowmem_virt_base );
}

static char *
lowmem_alloc( int size, cleanup_entry_t **ret_ce )
{
	ulong *pstart;
	cleanup_entry_t ce;
	int found=0;

	memset( &ce, 0, sizeof(ce) );
	if( ret_ce )
		*ret_ce = NULL;
	
	if( num_cleanup_entries >= MAX_NUM_CLEANUP_HANDLERS ) {
		printk("MOL: Need more cleanup slots!\n");
		return NULL;
	}

	/* Find big enough empty piece of memory */
	if( size < 0x10 )
		size = 0x10;

	pstart = lowmem_phys_to_virt(lowmem_phys_cursor);
	pstart = (ulong*)(((ulong)pstart + 0xf) & ~0xf);
	for( ; lowmem_phys_cursor < 0x3000; lowmem_phys_cursor+=4 ) {
		ulong *p = lowmem_phys_to_virt(lowmem_phys_cursor);
		if( ((int)p - (int)pstart) >= size ) {
			found = 1;
			break;
		}
		if( *p ) {
			pstart = (ulong*)(((ulong)p + sizeof(ulong) + 0xf) & ~0xf);
			continue;
		}
	}
	if( !found ) {
		printk("MOL: Did not find an empty piece of lowmem memory!\n");
		return NULL;
	}
	/* printk("lowmem alloc: %08lX\n", pstart ); */
	
	ce.lowmem_addr = (char*)pstart;
	ce.alloc_method = 0;
	ce.alloc_size = size;
	/* printk("lowmem-alloc SPACE %X bytes at %08lX\n", size, (ulong)pstart ); */

	cleanup_table[num_cleanup_entries] = ce;
	if( ret_ce )
		*ret_ce = &cleanup_table[num_cleanup_entries];
	num_cleanup_entries++;

	return ce.lowmem_addr;
}

static void
lowmem_free_all( void )
{
	cleanup_entry_t *ce = &cleanup_table[0];
	int i;

	for(i=0; i<num_cleanup_entries; i++, ce++ )
		memset( ce->lowmem_addr, 0, ce->alloc_size );

	num_cleanup_entries = 0;

	if( lowmem_mapping ) {
		unmap_phys_range( lowmem_mapping );
		lowmem_mapping = NULL;
	}
}


/************************************************************************/
/*	Hook implementation (see also molasm.h)				*/
/************************************************************************/

static int
relocate_inst( ulong *ret_opcode, ulong from, ulong to )
{
	ulong opcode = *ret_opcode;
	int offs=-1;

	/* XXX: UNTESTED if target instruction is a branch */
	
	/* Since we use this on the _first_ instruction of the
	 * exception vector, it can't touch LR/CR. Thus, we 
	 * only look for unconditional, relative branches.
	 */

	/* relativ branch b */
	if( (opcode & 0xfc000003) == (18<<26) ){
		offs = (opcode & 0x03fffffc);
		/* sign extend */
		if( offs & 0x03000000 )
			offs |= ~0x03ffffff;
	}
	/* unconditional, relativ bc branch (b 0100 001z1zz ...) */
	if( (opcode & 0xfe800003) == 0x42800000 ){
		offs = (opcode & 0xfffc);
		if( offs & 0x8000 )
			offs |= ~0xffff;
	}
	/* construct the absolute branch */
	if( offs != -1 ) {
		int dest = from + offs;
		if( dest < 0 || dest > 33554431 ) {
			printk("relocation of branch at %08lX to %08lX failed\n", from, to);
			return 1;
		}
		/* absolute branch */
		*ret_opcode = ((18<<26) + 2) | dest;
	}
	return 0;
}

/* pb[] = { vector, size, vret_offs, ...hook_code...  }  */
static inline int
action_reloc_hook( ulong *code, const ulong *pb )
{
	ulong addr, inst, vector=pb[0], size=pb[1], vret_offs=pb[2];
	cleanup_entry_t *clean;
	ulong *vector_virt, *target;
	char *lowmem;
	int i;
	
	/* Virtual address of exception vector */
	vector_virt = lowmem_phys_to_virt(vector);

	/* address of the vector hook code */
	addr = tophys_mol( (char*)code );

	/* allocate lowmem and add cleanup handler */
	if( !(lowmem=lowmem_alloc(size, &clean)) )
		return 1;

	/* printk("ACTION_RELOC_HOOK: %lx, %lx, %lx, %lx %p\n", vector, size, vret_action, vret_offs, lowmem); */

	memcpy( lowmem, &pb[3], size );

	/* perform the vret_action */
	for( i=0; reloc_table[i].action >= 0; i++ ) {
		reloc_table_entry_t *re = &reloc_table[i];
		
		/* insert the absolut branch */
		if( re->action == ACTION_VRET && re->offs == vector ) {
			target = (ulong*)(code_base + re->inst_offs);
			*target = ((18<<26) + 2) | lowmem_tophys(lowmem + vret_offs);
			flush_icache_mol( (ulong)target, (ulong)target + 4 );
			/* printk("'ba xxx' added (opcode %08lX at %p)\n", *target, target ); */
		}
	}

	/* fix the hook address in the glue code */
	target = (ulong*)lowmem;
	target[1] = (target[1] & ~0xffff) | (addr >> 16);	/* target[0] = addis r3,0,0 */
	target[3] = (target[3] & ~0xffff) | (addr & 0xffff);	/* target[1] = ori r3,r3,0 */

	/* relocate instruction to be overwritten with a branch */
	inst = *vector_virt;
	clean->opcode = inst;
	if( relocate_inst( &inst, vector, lowmem_tophys(lowmem+vret_offs) ))
		return 1;
	*(ulong*)(lowmem + vret_offs) = inst;
	flush_icache_mol( (ulong)lowmem, (ulong)lowmem + size );

	/* insert branch, 'ba lowmem_ph' */
	*(volatile ulong*)vector_virt = 0x48000002 + lowmem_tophys(lowmem);
	flush_icache_mol( (ulong)vector_virt, (ulong)vector_virt+4 );

	/* we are in business! */
	clean->inst_addr = vector_virt;
	return 0;
}

/* pb = { size, where_to_store_lowmem_addr, ...code... } */
static inline int
action_reloc_low( ulong *dummy_code, const ulong *pb )
{
	int size = pb[0];
	char **func_ptr = (char**)pb[1];
	char *lowmem;
	
	if( !(lowmem=lowmem_alloc(size, NULL)) )
		return 1;
	memcpy( lowmem, (char*)&pb[2], size );
	
	flush_icache_mol( (ulong)lowmem, (ulong)lowmem+size );
	*func_ptr = lowmem;
	return 0;
}

/* pb = { symind, size, fret_offset, codeglue... } */
static inline int
action_hook_function( ulong *code, const ulong *pb )
{
	ulong addr, fhook=pb[0], size=pb[1], fret_offs=pb[2];
	ulong *target, inst;
	char *lowmem, *func_addr=NULL;
	cleanup_entry_t *clean;

	switch( fhook ) {
#ifdef __linux__
	case FHOOK_FLUSH_HASH_PAGE:
		func_addr = (char*)compat_flush_hash_pages;
		break;
#endif
	default:
		printk("Bad fhook index %ld\n", fhook );
		return 1;
	}

	/* this does not have to be in lowmem, but it is simpler with a unified approach */
	if( !(lowmem=lowmem_alloc(size, &clean)) )
		return 1;

	/* printk("ACTION_HOOK_FUNCTION: %lx, %lx, %lx %p\n", fhook, size, fret_offs, lowmem); */

	memcpy( lowmem, &pb[3], size );

	/* fix the hook address in the glue code */
	target = (ulong*)lowmem;
	addr = (ulong)code;
	target[1] = (target[1] & ~0xffff) | (addr >> 16);	/* target[1] = addis rX,0,0 */
	target[2] = (target[2] & ~0xffff) | (addr & 0xffff);	/* target[2] = ori rX,rX,0 */

	/* relocate overwritten instruction and add relative return branch */
	inst = *(ulong*)func_addr;
	clean->opcode = inst;
	if( relocate_inst(&inst, (ulong)func_addr, (ulong)lowmem + fret_offs) )
		return 1;
	target = (ulong*)(lowmem + fret_offs);
	target[0] = inst;
	target[1] = (18<<26) | (((ulong)func_addr - (ulong)&target[1] + sizeof(long)) & 0x03fffffc);
	flush_icache_mol( (ulong)lowmem, (ulong)lowmem + size );
	_sync();
		
	/* insert relative branch, 'b lowmem' */
	*(volatile ulong*)func_addr = (18<<26) | ((lowmem - func_addr) & 0x03fffffc);
	flush_icache_mol( (ulong)func_addr, (ulong)func_addr+4 );

	_sync();

	/* We are in business! */
	clean->inst_addr = (ulong*)func_addr;
	return 0;
}

static int
do_hook_action( reloc_table_entry_t *re )
{
	ulong *pb = (ulong*)&r__reloctable_start[re->inst_offs] + 1;
	ulong *code = (ulong*)(code_base + re->offs);
	int code_size = r__reloctable_end - r__reloctable_start;

	if( re->offs >= code_size )
		return 1;

	switch( re->action ) {
	case ACTION_RELOC_HOOK:
		return action_reloc_hook( code, pb );

	case ACTION_HOOK_FUNCTION:
		return action_hook_function( code, pb );

	case ACTION_RELOCATE_LOW:
		return action_reloc_low( code, pb );

	case ACTION_VRET:
		return 0;
	}
	return 1;
}



/************************************************************************/
/*	write/remove hooks						*/
/************************************************************************/

int
write_hooks( void )
{
	int i,j, err=0;

	if( hooks_written )
		return 0;
	lowmem_initialize();

	for( i=FIRST_HOOK_ACTION; !err && i<=LAST_HOOK_ACTION ; i++ )
		for( j=0; !err && reloc_table[j].action >= 0 ; j++ )
			if( reloc_table[j].action == i )
				err += do_hook_action( &reloc_table[j] ) ? 1:0;
	if( err ) {
		printk("do_hook_action returned an error\n");
		remove_hooks();
		return 1;
	}
	hooks_written=1;
	return 0;
}


void
remove_hooks( void )
{
	cleanup_entry_t *ce = &cleanup_table[0];
	int i;

	if( hash_hook_handle ) {
		*hash_hook_handle = 0;
		hash_hook_handle = NULL;
	}

	for( i=0; i<num_cleanup_entries; i++, ce++ ) {
		if( ce->inst_addr ) {
			*(volatile ulong*)ce->inst_addr = cleanup_table[i].opcode;
			flush_icache_mol( (ulong)ce->inst_addr, (ulong)ce->inst_addr + 4 );
		}
	}
	lowmem_free_all();
	hooks_written = 0;
}

/*
 *   Creation Date: <97/06/16 18:02:12 samuel>
 *   Time-stamp: <2004/03/13 14:03:30 samuel>
 *
 *	<misc.h>
 *
 *
 *
 *   Copyright (C) 1997-2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#ifndef _H_MOD
#define _H_MOD

#ifdef __linux__

#include <linux/version.h>
#include <asm/uaccess.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define compat_verify_area(a,b,c)       ( ! access_ok(a,b,c) )
#else
#define compat_verify_area(a,b,c)       verify_area(a,b,c)
#endif

#endif /* __linux__ */

extern int 	g_num_sessions;			/* number of active sessions */

struct kernel_vars;

/* init.c */
extern int	common_init( void );
extern void	common_cleanup( void );
extern int	initialize_session( unsigned int sess_index );
extern void	destroy_session( unsigned int sess_index );
extern uint	get_session_magic( uint random_magic );

/* arch specific functions */
extern int	arch_common_init( void );
extern void	arch_common_cleanup( void );
extern struct kernel_vars *alloc_kvar_pages( void );
extern void	free_kvar_pages( struct kernel_vars *kv );
extern void	prevent_mod_unload( void );

/* misc.c */
struct dbg_op_params;
struct perf_ctr;
extern int	do_debugger_op( kernel_vars_t *kv, struct dbg_op_params *pb );
extern int	handle_ioctl( kernel_vars_t *kv, int what, int arg1, int arg2, int arg3 );

/* hostirq.c */
extern int grab_host_irq(kernel_vars_t *kv, int irq);
extern int release_host_irq(kernel_vars_t *kv, int irq);
extern void init_host_irqs(kernel_vars_t *kv);
extern void cleanup_host_irqs(kernel_vars_t *kv);

/* actions.c */
extern int	perform_actions( void );
extern void	cleanup_actions( void );

/* bit table manipulation */
static inline void
set_bit_mol( int nr, char *addr )
{
	ulong mask = 1 << (nr & 0x1f);
	ulong *p = ((ulong*)addr) + (nr >> 5);
	*p |= mask;
}

static inline void
clear_bit_mol( int nr, char *addr )
{
	ulong mask = 1 << (nr & 0x1f);
	ulong *p = ((ulong*)addr) + (nr >> 5);
	*p &= ~mask;
}

static inline int
check_bit_mol( int nr, char *addr )
{
	ulong mask = 1 << (nr & 0x1f);
	ulong *p = ((ulong*)addr) + (nr >> 5);
	return (*p & mask) != 0;
}

/* typesafe min/max (stolen from kernel.h) */
#define min_mol(x,y) ({ \
        const typeof(x) _x = (x);       \
        const typeof(y) _y = (y);       \
        (void) (&_x == &_y);            \
        _x < _y ? _x : _y; })

#define max_mol(x,y) ({ \
        const typeof(x) _x = (x);       \
        const typeof(y) _y = (y);       \
        (void) (&_x == &_y);            \
        _x > _y ? _x : _y; })

#endif

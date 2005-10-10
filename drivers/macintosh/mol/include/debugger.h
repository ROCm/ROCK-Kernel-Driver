/* 
 *   Creation Date: <1999/02/22 22:46:22 samuel>
 *   Time-stamp: <2003/07/27 14:42:05 samuel>
 *   
 *	<debugger.h>
 *	
 *	World interface of the debugger
 *   
 *   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_DEBUGGER
#define _H_DEBUGGER

#ifdef CONFIG_DEBUGGER
extern void 		debugger_init( void );
extern void 		debugger_cleanup( void );
extern int		debugger_enabled( void );
extern int		debugger_attached( void );
#else
static inline int 	debugger_enabled( void ) { return 0; }
static inline int 	debugger_attached( void ) { return 0; }
static inline void	debugger_init( void ) {}
static inline void	debugger_cleanup( void ) {}
#endif

/*******************************************/
/*	montior.c / nub.c		   */
/*******************************************/

extern void 	refresh_debugger_window( void );
extern void 	refresh_debugger( void );
extern void 	redraw_inst_win( void );
extern int	debugger_in_68k_mode( void );
extern void	debugger_nub_poll( void );

/* debug actions */
enum{  
	kDbgNOP=0, kDbgGo, kDbgGoRFI, kDbgStep, kDbgExit, kDbgStop, kDbgGoUser
};


/*******************************************/
/*	cmdline.c / nub.c		   */
/*******************************************/

/* put functions used exclusively in debugger mode in the dbg section */
#ifdef __linux__
#ifdef CONFIG_DEBUGGER
#define __dbg	__attribute__ ((__section__ (".moldbg")))
#define __dcmd __dbg
#else
#define __dbg	__attribute__ ((__section__ (".moldbg")))
#define __dcmd	inline __attribute__ ((__section__ (".moldbg")))
#endif
#else
#define __dbg
#define __dcmd inline
#endif

typedef struct {
	const char	*name;
	int		(*func)( int argc, char **argv );
	const char	*help;
} dbg_cmd_t;

typedef int	(*dbg_cmd_fp)( int argc, char **argv );

#ifdef CONFIG_DEBUGGER
extern void		add_cmd( const char *cmdname, const char *help,
				 int dummy, dbg_cmd_fp func );
extern void		add_dbg_cmds( dbg_cmd_t *table, int tablesize );
#else
#define add_cmd( a,b,c,d ) 	do {} while(0)
static inline void	add_dbg_cmds( dbg_cmd_t *table, int tablesize ) {}
#endif

/* for debugging */
#define HARD_BREAKPOINT \
	({ printm("Hardcoded breakpoint in '"__FILE__"'\n"); stop_emulation(); })


/*******************************************/
/*	mmu_cmds.c			   */
/*******************************************/

extern int	__dbg get_inst_context( void );
extern int	__dbg get_data_context( void );
extern int	__dbg ea_to_lvptr( ulong ea, int context, char **lvptr, int data_access );

#endif   /* _H_DEBUGGER */

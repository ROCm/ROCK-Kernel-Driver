/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_MODULE_H
#define _ASM_SN_MODULE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <linux/config.h>
#include <asm/sn/systeminfo.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/ksys/elsc.h>

#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#ifdef BRINGUP /* max. number of modules?  Should be about 300.*/
#define MODULE_MAX			56
#endif /* BRINGUP */
#define MODULE_MAX_NODES		1
#endif /* CONFIG_SGI_IP35 */
#define MODULE_HIST_CNT			16
#define MAX_MODULE_LEN			16

/* Well-known module IDs */
#define MODULE_UNKNOWN		(-2) /* initial value of klconfig brd_module */
/* #define INVALID_MODULE	(-1) ** generic invalid moduleid_t (arch.h) */
#define MODULE_NOT_SET		0    /* module ID not set in sys ctlrs. */

/* parameter for format_module_id() */
#define MODULE_FORMAT_BRIEF	1
#define MODULE_FORMAT_LONG	2


#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)

/*
 *	Module id format
 *
 *	  15-12 Brick type (enumerated)
 *	   11-6	Rack ID	(encoded class, group, number)
 *	    5-0 Brick position in rack (0-63)
 */
/*
 * Macros for getting the brick type
 */
#define MODULE_BTYPE_MASK	0xf000
#define MODULE_BTYPE_SHFT	12
#define MODULE_GET_BTYPE(_m)	(((_m) & MODULE_BTYPE_MASK) >> MODULE_BTYPE_SHFT)
#define MODULE_BT_TO_CHAR(_b)	(brick_types[(_b)])
#define MODULE_GET_BTCHAR(_m)	(MODULE_BT_TO_CHAR(MODULE_GET_BTYPE(_m)))

/*
 * Macros for getting the rack ID.
 */
#define MODULE_RACK_MASK	0x0fc0
#define MODULE_RACK_SHFT	6
#define MODULE_GET_RACK(_m)	(((_m) & MODULE_RACK_MASK) >> MODULE_RACK_SHFT)

/*
 * Macros for getting the brick position
 */
#define MODULE_BPOS_MASK	0x003f
#define MODULE_BPOS_SHFT	0
#define MODULE_GET_BPOS(_m)	(((_m) & MODULE_BPOS_MASK) >> MODULE_BPOS_SHFT)

/*
 * Macros for constructing moduleid_t's
 */
#define RBT_TO_MODULE(_r, _b, _t) ((_r) << MODULE_RACK_SHFT | \
				   (_b) << MODULE_BPOS_SHFT | \
				   (_t) << MODULE_BTYPE_SHFT)

/*
 * Macros for encoding and decoding rack IDs
 * A rack number consists of three parts:
 *   class	1 bit, 0==CPU/mixed, 1==I/O
 *   group	2 bits for CPU/mixed, 3 bits for I/O
 *   number	3 bits for CPU/mixed, 2 bits for I/O (1 based)
 */
#define RACK_GROUP_BITS(_r)	(RACK_GET_CLASS(_r) ? 3 : 2)
#define RACK_NUM_BITS(_r)	(RACK_GET_CLASS(_r) ? 2 : 3)

#define RACK_CLASS_MASK(_r)	0x20
#define RACK_CLASS_SHFT(_r)	5
#define RACK_GET_CLASS(_r)	\
	(((_r) & RACK_CLASS_MASK(_r)) >> RACK_CLASS_SHFT(_r))
#define RACK_ADD_CLASS(_r, _c)	\
	((_r) |= (_c) << RACK_CLASS_SHFT(_r) & RACK_CLASS_MASK(_r))

#define RACK_GROUP_SHFT(_r)	RACK_NUM_BITS(_r)
#define RACK_GROUP_MASK(_r)	\
	( (((unsigned)1<<RACK_GROUP_BITS(_r)) - 1) << RACK_GROUP_SHFT(_r) )
#define RACK_GET_GROUP(_r)	\
	(((_r) & RACK_GROUP_MASK(_r)) >> RACK_GROUP_SHFT(_r))
#define RACK_ADD_GROUP(_r, _g)	\
	((_r) |= (_g) << RACK_GROUP_SHFT(_r) & RACK_GROUP_MASK(_r))

#define RACK_NUM_SHFT(_r)	0
#define RACK_NUM_MASK(_r)	\
	( (((unsigned)1<<RACK_NUM_BITS(_r)) - 1) << RACK_NUM_SHFT(_r) )
#define RACK_GET_NUM(_r)	\
	( (((_r) & RACK_NUM_MASK(_r)) >> RACK_NUM_SHFT(_r)) + 1 )
#define RACK_ADD_NUM(_r, _n)	\
	((_r) |= ((_n) - 1) << RACK_NUM_SHFT(_r) & RACK_NUM_MASK(_r))

/*
 * Brick type definitions
 */
#define MAX_BRICK_TYPES		16 /* 1 << (MODULE_RACK_SHFT - MODULE_BTYPE_SHFT */

extern char brick_types[];

#define MODULE_CBRICK		0
#define MODULE_RBRICK		1
#define MODULE_IBRICK		2
#define MODULE_KBRICK		3
#define MODULE_XBRICK		4
#define MODULE_DBRICK		5
#define MODULE_PBRICK		6

/*
 * Moduleid_t comparison macros
 */
/* Don't compare the brick type:  only the position is significant */
#define MODULE_CMP(_m1, _m2)	(((_m1)&(MODULE_RACK_MASK|MODULE_BPOS_MASK)) -\
				 ((_m2)&(MODULE_RACK_MASK|MODULE_BPOS_MASK)))
#define MODULE_MATCH(_m1, _m2)	(MODULE_CMP((_m1),(_m2)) == 0)

#else

/*
 * Some code that uses this macro will not be conditionally compiled.
 */
#define MODULE_GET_BTCHAR(_m)	('?')
#define MODULE_CMP(_m1, _m2)	((_m1) - (_m2))
#define MODULE_MATCH(_m1, _m2)	(MODULE_CMP((_m1),(_m2)) == 0)

#endif /* CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 */

typedef struct module_s module_t;

struct module_s {
    moduleid_t		id;		/* Module ID of this module        */

    spinlock_t		lock;		/* Lock for this structure	   */

    /* List of nodes in this module */
    cnodeid_t		nodes[MODULE_MAX_NODES];
    int			nodecnt;	/* Number of nodes in array        */

    /* Fields for Module System Controller */
    int			mesgpend;	/* Message pending                 */
    int			shutdown;	/* Shutdown in progress            */
    struct semaphore	thdcnt;		/* Threads finished counter        */

    elsc_t		elsc;
    spinlock_t		elsclock;

    time_t		intrhist[MODULE_HIST_CNT];
    int			histptr;

    int			hbt_active;	/* MSC heartbeat monitor active    */
    uint64_t		hbt_last;	/* RTC when last heartbeat sent    */

    /* Module serial number info */
    union {
	char		snum_str[MAX_SERIAL_NUM_SIZE];	 /* used by CONFIG_SGI_IP27    */
	uint64_t	snum_int;			 /* used by speedo */
    } snum;
    int			snum_valid;

    int			disable_alert;
    int			count_down;
};

/* module.c */
extern module_t	       *modules[MODULE_MAX];	/* Indexed by cmoduleid_t   */
extern int		nummodules;

#ifndef CONFIG_IA64_SGI_IO
/* Clashes with LINUX stuff */
extern void		module_init(void);
#endif
extern module_t	       *module_lookup(moduleid_t id);

extern elsc_t	       *get_elsc(void);

extern int		get_kmod_info(cmoduleid_t cmod,
				      module_info_t *mod_info);

extern void		format_module_id(char *buffer, moduleid_t m, int fmt);
extern int		parse_module_id(char *buffer);

#ifdef	__cplusplus
}
#endif

#endif /* _ASM_SN_MODULE_H */

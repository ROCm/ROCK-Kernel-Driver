/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#ifndef	_ASM_SN_SN1_PROMLOG_H
#define	_ASM_SN_SN1_PROMLOG_H

#include <asm/sn/fprom.h>

#define PROMLOG_MAGIC			0x504c4f49
#define PROMLOG_VERSION			1

#define PROMLOG_OFFSET_MAGIC		0x10
#define PROMLOG_OFFSET_VERSION		0x14
#define PROMLOG_OFFSET_SEQUENCE		0x18
#define PROMLOG_OFFSET_ENTRY0		0x100

#define PROMLOG_ERROR_NONE		0
#define PROMLOG_ERROR_PROM	       -1
#define PROMLOG_ERROR_MAGIC	       -2
#define PROMLOG_ERROR_CORRUPT	       -3
#define PROMLOG_ERROR_BOL	       -4
#define PROMLOG_ERROR_EOL	       -5
#define PROMLOG_ERROR_POS	       -6
#define PROMLOG_ERROR_REPLACE	       -7
#define PROMLOG_ERROR_COMPACT	       -8
#define PROMLOG_ERROR_FULL	       -9
#define PROMLOG_ERROR_ARG	       -10
#define PROMLOG_ERROR_UNUSED	       -11	  	

#define PROMLOG_TYPE_UNUSED		0xf
#define PROMLOG_TYPE_LOG		3
#define PROMLOG_TYPE_LIST		2
#define PROMLOG_TYPE_VAR		1
#define PROMLOG_TYPE_DELETED		0

#define PROMLOG_TYPE_ANY		98
#define PROMLOG_TYPE_INVALID		99

#define PROMLOG_KEY_MAX			14
#define PROMLOG_VALUE_MAX		47
#define PROMLOG_CPU_MAX			4

typedef struct promlog_header_s {
    unsigned int	unused[4];
    unsigned int	magic;
    unsigned int	version;
    unsigned int	sequence;
} promlog_header_t;

typedef unsigned int promlog_pos_t;

typedef struct promlog_ent_s {		/* PROM individual entry */
    uint		type		: 4;
    uint		cpu_num		: 4;
    char		key[PROMLOG_KEY_MAX + 1];

    char		value[PROMLOG_VALUE_MAX + 1];

} promlog_ent_t;

typedef struct promlog_s {		/* Activation handle */
    fprom_t		f;
    int			sector_base;
    int			cpu_num;

    int			active;		/* Active sector, 0 or 1 */

    promlog_pos_t	log_start;
    promlog_pos_t	log_end;

    promlog_pos_t	alt_start;
    promlog_pos_t	alt_end;

    promlog_pos_t	pos;
    promlog_ent_t	ent;
} promlog_t;

#endif /* _ASM_SN_SN1_PROMLOG_H */

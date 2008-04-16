/*
 *	Copyright (C) 2007 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor submodule (match) prototypes
 */

#ifndef __MATCH_H
#define __MATCH_H

#define DFA_START			1

/**
 * The format used for transition tables is based on the GNU flex table
 * file format (--tables-file option; see Table File Format in the flex
 * info pages and the flex sources for documentation). The magic number
 * used in the header is 0x1B5E783D insted of 0xF13C57B1 though, because
 * the YY_ID_CHK (check) and YY_ID_DEF (default) tables are used
 * slightly differently (see the apparmor-parser package).
 */

#define YYTH_MAGIC	0x1B5E783D

struct table_set_header {
	u32		th_magic;	/* YYTH_MAGIC */
	u32		th_hsize;
	u32		th_ssize;
	u16		th_flags;
	char		th_version[];
};

#define	YYTD_ID_ACCEPT	1
#define YYTD_ID_BASE	2
#define YYTD_ID_CHK	3
#define YYTD_ID_DEF	4
#define YYTD_ID_EC	5
#define YYTD_ID_META	6
#define YYTD_ID_ACCEPT2 7
#define YYTD_ID_NXT	8


#define YYTD_DATA8	1
#define YYTD_DATA16	2
#define YYTD_DATA32	4

struct table_header {
	u16		td_id;
	u16		td_flags;
	u32		td_hilen;
	u32		td_lolen;
	char		td_data[];
};

#define DEFAULT_TABLE(DFA) ((u16 *)((DFA)->tables[YYTD_ID_DEF - 1]->td_data))
#define BASE_TABLE(DFA) ((u32 *)((DFA)->tables[YYTD_ID_BASE - 1]->td_data))
#define NEXT_TABLE(DFA) ((u16 *)((DFA)->tables[YYTD_ID_NXT - 1]->td_data))
#define CHECK_TABLE(DFA) ((u16 *)((DFA)->tables[YYTD_ID_CHK - 1]->td_data))
#define EQUIV_TABLE(DFA) ((u8 *)((DFA)->tables[YYTD_ID_EC - 1]->td_data))
#define ACCEPT_TABLE(DFA) ((u32 *)((DFA)->tables[YYTD_ID_ACCEPT - 1]->td_data))
#define ACCEPT_TABLE2(DFA) ((u32 *)((DFA)->tables[YYTD_ID_ACCEPT2 -1]->td_data))

struct aa_dfa {
	struct table_header *tables[YYTD_ID_NXT];
};

#define byte_to_byte(X) (X)

#define UNPACK_ARRAY(TABLE, BLOB, LEN, TYPE, NTOHX) \
	do { \
		typeof(LEN) __i; \
		TYPE *__t = (TYPE *) TABLE; \
		TYPE *__b = (TYPE *) BLOB; \
		for (__i = 0; __i < LEN; __i++) { \
			__t[__i] = NTOHX(__b[__i]); \
		} \
	} while (0)

static inline size_t table_size(size_t len, size_t el_size)
{
	return ALIGN(sizeof(struct table_header) + len * el_size, 8);
}

#endif /* __MATCH_H */

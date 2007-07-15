/*
 *	Copyright (C) 2007 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	Regular expression transition table matching
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include "apparmor.h"
#include "match.h"

static struct table_header *unpack_table(void *blob, size_t bsize)
{
	struct table_header *table = NULL;
	struct table_header th;
	size_t tsize;

	if (bsize < sizeof(struct table_header))
		goto out;

	th.td_id = be16_to_cpu(*(u16 *) (blob));
	th.td_flags = be16_to_cpu(*(u16 *) (blob + 2));
	th.td_lolen = be32_to_cpu(*(u32 *) (blob + 8));
	blob += sizeof(struct table_header);

	if (!(th.td_flags == YYTD_DATA16 || th.td_flags == YYTD_DATA32 ||
		th.td_flags == YYTD_DATA8))
		goto out;

	tsize = table_size(th.td_lolen, th.td_flags);
	if (bsize < tsize)
		goto out;

	table = kmalloc(tsize, GFP_KERNEL);
	if (table) {
		*table = th;
		if (th.td_flags == YYTD_DATA8)
			UNPACK_ARRAY(table->td_data, blob, th.td_lolen,
				     u8, byte_to_byte);
		else if (th.td_flags == YYTD_DATA16)
			UNPACK_ARRAY(table->td_data, blob, th.td_lolen,
				     u16, be16_to_cpu);
		else
			UNPACK_ARRAY(table->td_data, blob, th.td_lolen,
				     u32, be32_to_cpu);
	}

out:
	return table;
}

int unpack_dfa(struct aa_dfa *dfa, void *blob, size_t size)
{
	int hsize, i;
	int error = -ENOMEM;

	/* get dfa table set header */
	if (size < sizeof(struct table_set_header))
		goto fail;

	if (ntohl(*(u32 *)blob) != YYTH_MAGIC)
		goto fail;

	hsize = ntohl(*(u32 *)(blob + 4));
	if (size < hsize)
		goto fail;

	blob += hsize;
	size -= hsize;

	error = -EPROTO;
	while (size > 0) {
		struct table_header *table;
		table = unpack_table(blob, size);
		if (!table)
			goto fail;

		switch(table->td_id) {
		case YYTD_ID_ACCEPT:
		case YYTD_ID_BASE:
			dfa->tables[table->td_id - 1] = table;
			if (table->td_flags != YYTD_DATA32)
				goto fail;
			break;
		case YYTD_ID_DEF:
		case YYTD_ID_NXT:
		case YYTD_ID_CHK:
			dfa->tables[table->td_id - 1] = table;
			if (table->td_flags != YYTD_DATA16)
				goto fail;
			break;
		case YYTD_ID_EC:
			dfa->tables[table->td_id - 1] = table;
			if (table->td_flags != YYTD_DATA8)
				goto fail;
			break;
		default:
			kfree(table);
			goto fail;
		}

		blob += table_size(table->td_lolen, table->td_flags);
		size -= table_size(table->td_lolen, table->td_flags);
	}

	return 0;

fail:
	for (i = 0; i < ARRAY_SIZE(dfa->tables); i++) {
		if (dfa->tables[i]) {
			kfree(dfa->tables[i]);
			dfa->tables[i] = NULL;
		}
	}
	return error;
}

/**
 * verify_dfa - verify that all the transitions and states in the dfa tables
 *              are in bounds.
 * @dfa: dfa to test
 *
 * assumes dfa has gone through the verification done by unpacking
 */
int verify_dfa(struct aa_dfa *dfa)
{
	size_t i, state_count, trans_count;
	int error = -EPROTO;

	/* check that required tables exist */
	if (!(dfa->tables[YYTD_ID_ACCEPT -1 ] &&
	      dfa->tables[YYTD_ID_DEF - 1] &&
	      dfa->tables[YYTD_ID_BASE - 1] &&
	      dfa->tables[YYTD_ID_NXT - 1] &&
	      dfa->tables[YYTD_ID_CHK - 1]))
		goto out;

	/* accept.size == default.size == base.size */
	state_count = dfa->tables[YYTD_ID_BASE - 1]->td_lolen;
	if (!(state_count == dfa->tables[YYTD_ID_DEF - 1]->td_lolen &&
	      state_count == dfa->tables[YYTD_ID_ACCEPT - 1]->td_lolen))
		goto out;

	/* next.size == chk.size */
	trans_count = dfa->tables[YYTD_ID_NXT - 1]->td_lolen;
	if (trans_count != dfa->tables[YYTD_ID_CHK - 1]->td_lolen)
		goto out;

	/* if equivalence classes then its table size must be 256 */
	if (dfa->tables[YYTD_ID_EC - 1] &&
	    dfa->tables[YYTD_ID_EC - 1]->td_lolen != 256)
		goto out;

	for (i = 0; i < state_count; i++) {
		if (DEFAULT_TABLE(dfa)[i] >= state_count)
			goto out;
		if (BASE_TABLE(dfa)[i] >= trans_count + 256)
			goto out;
	}

	for (i = 0; i < trans_count ; i++) {
		if (NEXT_TABLE(dfa)[i] >= state_count)
			goto out;
		if (CHECK_TABLE(dfa)[i] >= state_count)
			goto out;
	}

	/* verify accept permissions */
	for (i = 0; i < state_count; i++) {
		int mode = ACCEPT_TABLE(dfa)[i];

		if (mode & ~AA_VALID_PERM_MASK)
			goto out;

		/* if MAY_EXEC, exactly one exec modifier must be set */
		if (mode & MAY_EXEC) {
			mode &= AA_EXEC_MODIFIERS;
			if (mode & (mode - 1))
				goto out;
		}
	}

	error = 0;
out:
	return error;
}

struct aa_dfa *aa_match_alloc(void)
{
	return kzalloc(sizeof(struct aa_dfa), GFP_KERNEL);
}

void aa_match_free(struct aa_dfa *dfa)
{
	if (dfa) {
		int i;

		for (i = 0; i < ARRAY_SIZE(dfa->tables); i++)
			kfree(dfa->tables[i]);
	}
	kfree(dfa);
}

/**
 * aa_dfa_match - match @path against @dfa starting in @state
 * @dfa: the dfa to match @path against
 * @state: the state to start matching in
 * @path: the path to match against the dfa
 *
 * aa_dfa_match will match the full path length and return the state it
 * finished matching in. The final state is used to look up the accepting
 * label.
 */
unsigned int aa_dfa_match(struct aa_dfa *dfa, const char *str)
{
	u16 *def = DEFAULT_TABLE(dfa);
	u32 *base = BASE_TABLE(dfa);
	u16 *next = NEXT_TABLE(dfa);
	u16 *check = CHECK_TABLE(dfa);
	unsigned int state = 1, pos;

	/* current state is <state>, matching character *str */
	if (dfa->tables[YYTD_ID_EC - 1]) {
		u8 *equiv = EQUIV_TABLE(dfa);
		while (*str) {
			pos = base[state] + equiv[(u8)*str++];
			if (check[pos] == state)
				state = next[pos];
			else
				state = def[state];
		}
	} else {
		while (*str) {
			pos = base[state] + (u8)*str++;
			if (check[pos] == state)
				state = next[pos];
			else
				state = def[state];
		}
	}
	return ACCEPT_TABLE(dfa)[state];
}

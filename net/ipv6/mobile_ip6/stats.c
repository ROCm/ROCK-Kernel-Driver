/*
 *      Statistics module
 *
 *      Authors:
 *      Sami Kivisaari          <skivisaa@cc.hut.fi>
 *
 *      $Id: s.stats.c 1.13 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *     
 *      Changes:
 *      Krishna Kumar, 
 *      Venkata Jagana   :  SMP locking fix  
 */

#include <linux/config.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include "stats.h"

struct mipv6_statistics mipv6_stats;

static int proc_info_dump(
	char *buffer, char **start,
	off_t offset, int length)
{
	struct inf {
		char *name;
		int *value;
	} int_stats[] = {
		{"NEncapsulations", &mipv6_stats.n_encapsulations},
		{"NDecapsulations", &mipv6_stats.n_decapsulations},
		{"NBindRefreshRqsRcvd", &mipv6_stats.n_brr_rcvd},
		{"NHomeTestInitsRcvd", &mipv6_stats.n_hoti_rcvd},
		{"NCareofTestInitsRcvd", &mipv6_stats.n_coti_rcvd},
		{"NHomeTestRcvd", &mipv6_stats.n_hot_rcvd},
		{"NCareofTestRcvd", &mipv6_stats.n_cot_rcvd},
		{"NBindUpdatesRcvd", &mipv6_stats.n_bu_rcvd},
		{"NBindAcksRcvd", &mipv6_stats.n_ba_rcvd},
		{"NBindNAcksRcvd", &mipv6_stats.n_ban_rcvd},
		{"NBindErrorsRcvd", &mipv6_stats.n_be_rcvd},
		{"NBindRefreshRqsSent", &mipv6_stats.n_brr_sent},
		{"NHomeTestInitsSent", &mipv6_stats.n_hoti_sent},
		{"NCareofTestInitsSent", &mipv6_stats.n_coti_sent},
		{"NHomeTestSent", &mipv6_stats.n_hot_sent},
		{"NCareofTestSent", &mipv6_stats.n_cot_sent},
		{"NBindUpdatesSent", &mipv6_stats.n_bu_sent},
		{"NBindAcksSent", &mipv6_stats.n_ba_sent},
		{"NBindNAcksSent", &mipv6_stats.n_ban_sent},
		{"NBindErrorsSent", &mipv6_stats.n_be_sent},
		{"NBindUpdatesDropAuth", &mipv6_stats.n_bu_drop.auth},
		{"NBindUpdatesDropInvalid", &mipv6_stats.n_bu_drop.invalid},
		{"NBindUpdatesDropMisc", &mipv6_stats.n_bu_drop.misc},
		{"NBindAcksDropAuth", &mipv6_stats.n_bu_drop.auth},
		{"NBindAcksDropInvalid", &mipv6_stats.n_bu_drop.invalid},
		{"NBindAcksDropMisc", &mipv6_stats.n_bu_drop.misc},
		{"NBindRqsDropAuth", &mipv6_stats.n_bu_drop.auth},
		{"NBindRqsDropInvalid", &mipv6_stats.n_bu_drop.invalid},
		{"NBindRqsDropMisc", &mipv6_stats.n_bu_drop.misc}
	};

	int i;
	int len = 0;
	for(i=0; i<sizeof(int_stats) / sizeof(struct inf); i++) {
		len += sprintf(buffer + len, "%s = %d\n",
			       int_stats[i].name, *int_stats[i].value);
	}

	*start = buffer + offset;

	len -= offset;

	if(len > length) len = length;

	return len;
}

int mipv6_stats_init(void)
{
	memset(&mipv6_stats, 0, sizeof(struct mipv6_statistics));
	proc_net_create("mip6_stat", 0, proc_info_dump);
	return 0;
}

void mipv6_stats_exit(void)
{
	proc_net_remove("mip6_stat");
}

EXPORT_SYMBOL(mipv6_stats);

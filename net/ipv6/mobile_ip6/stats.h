/*
 *      MIPL Mobile IPv6 Statistics header file
 *
 *      $Id: s.stats.h 1.11 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _STATS_H
#define _STATS_H

struct mipv6_drop {
	__u32 auth;
	__u32 invalid;
	__u32 misc;
};

struct mipv6_statistics {
	int n_encapsulations;
	int n_decapsulations;
	int n_mh_in_msg;
	int n_mh_in_error;
	int n_mh_out_msg;
	int n_mh_out_error;

	int n_brr_rcvd;
	int n_hoti_rcvd;
	int n_coti_rcvd;
	int n_hot_rcvd;
	int n_cot_rcvd;
	int n_bu_rcvd;
	int n_ba_rcvd;
	int n_ban_rcvd;
	int n_be_rcvd;

	int n_brr_sent;
	int n_hoti_sent;
	int n_coti_sent;
	int n_hot_sent;
	int n_cot_sent;
	int n_bu_sent;
	int n_ba_sent;
	int n_ban_sent;
	int n_be_sent;

	int n_ha_rcvd;
	int n_ha_sent;

	struct mipv6_drop n_bu_drop;
	struct mipv6_drop n_ba_drop;
	struct mipv6_drop n_brr_drop;
	struct mipv6_drop n_be_drop;
	struct mipv6_drop n_ha_drop;
};

extern struct mipv6_statistics mipv6_stats;

#ifdef CONFIG_SMP
/* atomic_t is max 24 bits long */
#define MIPV6_INC_STATS(X) atomic_inc((atomic_t *)&mipv6_stats.X);
#else
#define MIPV6_INC_STATS(X) mipv6_stats.X++;
#endif

int mipv6_stats_init(void);
void mipv6_stats_exit(void);

#endif

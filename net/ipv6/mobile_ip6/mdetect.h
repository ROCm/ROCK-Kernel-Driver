/*
 *      MIPL Mobile IPv6 Movement detection module header file
 *
 *      $Id: s.mdetect.h 1.33 03/09/30 11:01:58+03:00 henkku@mart10.hut.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _MDETECT_H
#define _MDETECT_H

struct handoff {
	int home_address; /* Is the coa a home address */
	int ifindex;
	int plen;
	struct in6_addr *coa;
	struct in6_addr rtr_addr; /* Prefix or rtr address if coa is home address */
};

int mipv6_initialize_mdetect(void);

int mipv6_shutdown_mdetect(void);

int mipv6_get_care_of_address(struct in6_addr *homeaddr, struct in6_addr *coa);

int mipv6_mdet_del_if(int ifindex);

int mipv6_mdet_finalize_ho(const struct in6_addr *coa, const int ifindex);

#endif /* _MDETECT_H */

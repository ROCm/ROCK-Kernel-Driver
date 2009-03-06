/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides public API for filter resource.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
 * Certain parts of the driver were implemented by
 *          Alexandra Kossovsky <Alexandra.Kossovsky@oktetlabs.ru>
 *          OKTET Labs Ltd, Russia,
 *          http://oktetlabs.ru, <info@oktetlabs.ru>
 *          by request of Solarflare Communications
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#ifndef __CI_EFRM_FILTER_H__
#define __CI_EFRM_FILTER_H__

#include <ci/efrm/resource.h>
#include <ci/efhw/common.h>


struct filter_resource;
struct vi_resource;
struct efrm_client;


/*!
 * Allocate filter resource.
 *
 * \param vi_parent VI resource to use as parent. The function takes
 *                  reference to the VI resource on success.
 * \param frs_out   pointer to return the new filter resource
 *
 * \return          status code; if non-zero, frs_out is unchanged
 */
extern int
efrm_filter_resource_alloc(struct vi_resource *vi_parent,
			   struct filter_resource **frs_out);

extern void
efrm_filter_resource_release(struct filter_resource *);


extern int efrm_filter_resource_clear(struct filter_resource *frs);

extern int __efrm_filter_resource_set(struct filter_resource *frs, int type,
				      unsigned saddr_be32, uint16_t sport_be16,
				      unsigned daddr_be32, uint16_t dport_be16);

static inline int
efrm_filter_resource_tcp_set(struct filter_resource *frs,
			     unsigned saddr, uint16_t sport,
			     unsigned daddr, uint16_t dport)
{
	int type;

	EFRM_ASSERT((saddr && sport) || (!saddr && !sport));

	type =
	    saddr ? EFHW_IP_FILTER_TYPE_TCP_FULL :
	    EFHW_IP_FILTER_TYPE_TCP_WILDCARD;

	return __efrm_filter_resource_set(frs, type,
					  saddr, sport, daddr, dport);
}

static inline int
efrm_filter_resource_udp_set(struct filter_resource *frs,
			     unsigned saddr, uint16_t sport,
			     unsigned daddr, uint16_t dport)
{
	int type;

	EFRM_ASSERT((saddr && sport) || (!saddr && !sport));

	type =
	    saddr ? EFHW_IP_FILTER_TYPE_UDP_FULL :
	    EFHW_IP_FILTER_TYPE_UDP_WILDCARD;

	return __efrm_filter_resource_set(frs,
					  type, saddr, sport, daddr, dport);
}


extern int
efrm_filter_resource_instance(struct filter_resource *);

extern struct efrm_resource *
efrm_filter_resource_to_resource(struct filter_resource *);

extern struct filter_resource *
efrm_filter_resource_from_resource(struct efrm_resource *);

extern void
efrm_filter_resource_free(struct filter_resource *);


#endif /* __CI_EFRM_FILTER_H__ */
/*! \cidoxg_end */

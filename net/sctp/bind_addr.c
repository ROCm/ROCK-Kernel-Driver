/* SCTP kernel reference Implementation
 * Copyright (c) Cisco 1999,2000
 * Copyright (c) Motorola 1999,2000,2001
 * Copyright (c) International Business Machines Corp., 2001,2002
 * Copyright (c) La Monte H.P. Yarroll 2001
 *
 * This file is part of the SCTP kernel reference implementation.
 *
 * A collection class to handle the storage of transport addresses.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Daisy Chang           <daisyc@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/if_inet6.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Forward declarations for internal helpers. */
static int sctp_copy_one_addr(sctp_bind_addr_t *, union sctp_addr *,
			      sctp_scope_t scope, int gfp, int flags);
static void sctp_bind_addr_clean(sctp_bind_addr_t *);

/* First Level Abstractions. */

/* Copy 'src' to 'dest' taking 'scope' into account.  Omit addresses
 * in 'src' which have a broader scope than 'scope'.
 */
int sctp_bind_addr_copy(sctp_bind_addr_t *dest, const sctp_bind_addr_t *src,
			sctp_scope_t scope, int gfp, int flags)
{
	struct sockaddr_storage_list *addr;
	struct list_head *pos;
	int error = 0;

	/* All addresses share the same port.  */
	dest->port = src->port;

	/* Extract the addresses which are relevant for this scope.  */
	list_for_each(pos, &src->address_list) {
		addr = list_entry(pos, struct sockaddr_storage_list, list);
		error = sctp_copy_one_addr(dest, &addr->a, scope,
					   gfp, flags);
		if (error < 0)
			goto out;
	}

	/* If there are no addresses matching the scope and
	 * this is global scope, try to get a link scope address, with
	 * the assumption that we must be sitting behind a NAT.
	 */
	if (list_empty(&dest->address_list) && (SCTP_SCOPE_GLOBAL == scope)) {
		list_for_each(pos, &src->address_list) {
			addr = list_entry(pos, struct sockaddr_storage_list,
					  list);
			error = sctp_copy_one_addr(dest, &addr->a,
						   SCTP_SCOPE_LINK, gfp,
						   flags);
			if (error < 0)
				goto out;
		}
	}

out:
	if (error)
		sctp_bind_addr_clean(dest);

	return error;
}

/* Create a new SCTP_bind_addr from nothing.  */
sctp_bind_addr_t *sctp_bind_addr_new(int gfp)
{
	sctp_bind_addr_t *retval;

	retval = t_new(sctp_bind_addr_t, gfp);
	if (!retval)
		goto nomem;

	sctp_bind_addr_init(retval, 0);
	retval->malloced = 1;
	SCTP_DBG_OBJCNT_INC(bind_addr);

nomem:
	return retval;
}

/* Initialize the SCTP_bind_addr structure for either an endpoint or
 * an association.
 */
void sctp_bind_addr_init(sctp_bind_addr_t *bp, __u16 port)
{
	bp->malloced = 0;

	INIT_LIST_HEAD(&bp->address_list);
	bp->port = port;
}

/* Dispose of the address list. */
static void sctp_bind_addr_clean(sctp_bind_addr_t *bp)
{
	struct sockaddr_storage_list *addr;
	struct list_head *pos, *temp;

	/* Empty the bind address list. */
	list_for_each_safe(pos, temp, &bp->address_list) {
		addr = list_entry(pos, struct sockaddr_storage_list, list);
		list_del(pos);
		kfree(addr);
		SCTP_DBG_OBJCNT_DEC(addr);
	}
}

/* Dispose of an SCTP_bind_addr structure  */
void sctp_bind_addr_free(sctp_bind_addr_t *bp)
{
	/* Empty the bind address list. */
	sctp_bind_addr_clean(bp);

	if (bp->malloced) {
		kfree(bp);
		SCTP_DBG_OBJCNT_DEC(bind_addr);
	}
}

/* Add an address to the bind address list in the SCTP_bind_addr structure. */
int sctp_add_bind_addr(sctp_bind_addr_t *bp, union sctp_addr *new,
		       int gfp)
{
	struct sockaddr_storage_list *addr;

	/* Add the address to the bind address list.  */
	addr = t_new(struct sockaddr_storage_list, gfp);
	if (!addr)
		return -ENOMEM;

	addr->a = *new;

	/* Fix up the port if it has not yet been set.
	 * Both v4 and v6 have the port at the same offset.
	 */
	if (!addr->a.v4.sin_port)
		addr->a.v4.sin_port = bp->port;

	INIT_LIST_HEAD(&addr->list);
	list_add_tail(&addr->list, &bp->address_list);
	SCTP_DBG_OBJCNT_INC(addr);

	return 0;
}

/* Delete an address from the bind address list in the SCTP_bind_addr
 * structure.
 */
int sctp_del_bind_addr(sctp_bind_addr_t *bp, union sctp_addr *del_addr)
{
	struct list_head *pos, *temp;
	struct sockaddr_storage_list *addr;

	list_for_each_safe(pos, temp, &bp->address_list) {
		addr = list_entry(pos, struct sockaddr_storage_list, list);
		if (sctp_cmp_addr_exact(&addr->a, del_addr)) {
			/* Found the exact match. */
			list_del(pos);
			kfree(addr);
			SCTP_DBG_OBJCNT_DEC(addr);

			return 0;
		}
	}

	return -EINVAL;
}

/* Create a network byte-order representation of all the addresses
 * formated as SCTP parameters.
 *
 * The second argument is the return value for the length.
 */
union sctp_params sctp_bind_addrs_to_raw(const sctp_bind_addr_t *bp,
					 int *addrs_len, int gfp)
{
	union sctp_params addrparms;
	union sctp_params retval;
	int addrparms_len;
	sctp_addr_param_t rawaddr;
	int len;
	struct sockaddr_storage_list *addr;
	struct list_head *pos;
	addrparms_len = 0;
	len = 0;

	/* Allocate enough memory at once. */
	list_for_each(pos, &bp->address_list) {
		len += sizeof(sctp_addr_param_t);
	}

	/* Don't even bother embedding an address if there
	 * is only one.
	 */
	if (len == sizeof(sctp_addr_param_t)) {
		retval.v = NULL;
		goto end_raw;
	}

	retval.v = kmalloc(len, gfp);
	if (!retval.v)
		goto end_raw;

	addrparms = retval;

	list_for_each(pos, &bp->address_list) {
		addr = list_entry(pos, struct sockaddr_storage_list, list);
		len = sockaddr2sctp_addr(&addr->a, &rawaddr);
		memcpy(addrparms.v, &rawaddr, len);
		addrparms.v += len;
		addrparms_len += len;
	}

end_raw:
	*addrs_len = addrparms_len;
	return retval;
}

/*
 * Create an address list out of the raw address list format (IPv4 and IPv6
 * address parameters).
 */
int sctp_raw_to_bind_addrs(sctp_bind_addr_t *bp, __u8 *raw_addr_list,
			   int addrs_len, __u16 port, int gfp)
{
	sctp_addr_param_t *rawaddr;
	sctp_paramhdr_t *param;
	union sctp_addr addr;
	int retval = 0;
	int len;

	/* Convert the raw address to standard address format */
	while (addrs_len) {
		param = (sctp_paramhdr_t *)raw_addr_list;
		rawaddr = (sctp_addr_param_t *)raw_addr_list;

		switch (param->type) {
		case SCTP_PARAM_IPV4_ADDRESS:
		case SCTP_PARAM_IPV6_ADDRESS:
			sctp_param2sockaddr(&addr, rawaddr, port, 0);
			retval = sctp_add_bind_addr(bp, &addr, gfp);
			if (retval) {
				/* Can't finish building the list, clean up. */
				sctp_bind_addr_clean(bp);
				break;;
			}
			len = ntohs(param->length);
			addrs_len -= len;
			raw_addr_list += len;
			break;
		default:
			/* Corrupted raw addr list! */
			retval = -EINVAL;
			sctp_bind_addr_clean(bp);
			break;
		}
		if (retval)
			break;
	}

	return retval;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/* Does this contain a specified address?  Allow wildcarding. */
int sctp_bind_addr_match(sctp_bind_addr_t *bp, const union sctp_addr *addr,
			 struct sctp_opt *opt)
{
	struct sockaddr_storage_list *laddr;
	struct list_head *pos;

	list_for_each(pos, &bp->address_list) {
		laddr = list_entry(pos, struct sockaddr_storage_list, list);
		if (opt->pf->cmp_addr(&laddr->a, addr, opt))
 			return 1;
	}

	return 0;
}

/* Copy out addresses from the global local address list. */
static int sctp_copy_one_addr(sctp_bind_addr_t *dest, union sctp_addr *addr,
			      sctp_scope_t scope, int gfp, int flags)
{
	struct sctp_protocol *proto = sctp_get_protocol();
	int error = 0;

	if (sctp_is_any(addr)) {
		error = sctp_copy_local_addr_list(proto, dest, scope,
						  gfp, flags);
	} else if (sctp_in_scope(addr, scope)) {
		/* Now that the address is in scope, check to see if
		 * the address type is supported by local sock as
		 * well as the remote peer.
		 */
		if ((((AF_INET == addr->sa.sa_family) &&
		      (flags & SCTP_ADDR4_PEERSUPP))) ||
		    (((AF_INET6 == addr->sa.sa_family) &&
		      (flags & SCTP_ADDR6_ALLOWED) &&
		      (flags & SCTP_ADDR6_PEERSUPP))))
			error = sctp_add_bind_addr(dest, addr, gfp);
	}

	return error;
}

/* Is this a wildcard address?  */
int sctp_is_any(const union sctp_addr *addr)
{
	struct sctp_af *af = sctp_get_af_specific(addr->sa.sa_family);
	if (!af)
		return 0;
	return af->is_any(addr);
}

/* Is 'addr' valid for 'scope'?  */
int sctp_in_scope(const union sctp_addr *addr, sctp_scope_t scope)
{
	sctp_scope_t addr_scope = sctp_scope(addr);

	/* The unusable SCTP addresses will not be considered with
	 * any defined scopes.
	 */
	if (SCTP_SCOPE_UNUSABLE == addr_scope)
		return 0;
	/*
	 * For INIT and INIT-ACK address list, let L be the level of
	 * of requested destination address, sender and receiver
	 * SHOULD include all of its addresses with level greater
	 * than or equal to L.
	 */
	if (addr_scope <= scope)
		return 1;

	return 0;
}

/********************************************************************
 * 3rd Level Abstractions
 ********************************************************************/

/* What is the scope of 'addr'?  */
sctp_scope_t sctp_scope(const union sctp_addr *addr)
{
	struct sctp_af *af;

	af = sctp_get_af_specific(addr->sa.sa_family);
	if (!af)
		return SCTP_SCOPE_UNUSABLE;

	return af->scope((union sctp_addr *)addr);
}

/* SCTP kernel reference Implementation
 * Copyright (c) Cisco 1999,2000 
 * Copyright (c) Motorola 1999,2000,2001 
 * Copyright (c) International Business Machines Corp., 2001
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
static int sctp_copy_one_addr(sctp_bind_addr_t *, sockaddr_storage_t *,
			      sctp_scope_t scope, int priority, int flags);
static void sctp_bind_addr_clean(sctp_bind_addr_t *);

/* First Level Abstractions. */

/* Copy 'src' to 'dest' taking 'scope' into account.  Omit addresses
 * in 'src' which have a broader scope than 'scope'.
 */
int sctp_bind_addr_copy(sctp_bind_addr_t *dest, const sctp_bind_addr_t *src,
			sctp_scope_t scope, int priority, int flags)
{
	struct sockaddr_storage_list *addr;
	list_t *pos;
	int error = 0;

	/* All addresses share the same port.  */
	dest->port = src->port;

	/* Extract the addresses which are relevant for this scope.  */
	list_for_each(pos, &src->address_list) {
		addr = list_entry(pos, struct sockaddr_storage_list, list);
		error = sctp_copy_one_addr(dest, &addr->a, scope,
					   priority, flags);
		if (error < 0)
			goto out;
	}

out:
	if (error)
		sctp_bind_addr_clean(dest);
	
	return error;
}

/* Create a new SCTP_bind_addr from nothing.  */
sctp_bind_addr_t *sctp_bind_addr_new(int priority)
{
	sctp_bind_addr_t *retval;

	retval = t_new(sctp_bind_addr_t, priority);
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
	list_t *pos, *temp;

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
int sctp_add_bind_addr(sctp_bind_addr_t *bp, sockaddr_storage_t *new,
		       int priority)
{
	struct sockaddr_storage_list *addr;

	/* Add the address to the bind address list.  */
	addr = t_new(struct sockaddr_storage_list, priority);
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
int sctp_del_bind_addr(sctp_bind_addr_t *bp, sockaddr_storage_t *del_addr)
{
	list_t *pos, *temp;
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
sctpParam_t sctp_bind_addrs_to_raw(const sctp_bind_addr_t *bp, int *addrs_len,
				   int priority)
{
	sctpParam_t rawaddr;
	sctpParam_t addrparms;
	sctpParam_t retval;
	int addrparms_len;
	sctpIpAddress_t rawaddr_space;
	int len;
	struct sockaddr_storage_list *addr;
	list_t *pos;

	retval.v = NULL;
	addrparms_len = 0;
	len = 0;

	/* Allocate enough memory at once. */
	list_for_each(pos, &bp->address_list) {
		len += sizeof(sctp_ipv6addr_param_t);
	}

	addrparms.v = kmalloc(len, priority);
	if (!addrparms.v)
		goto end_raw;

	retval = addrparms;
	rawaddr.v4 = &rawaddr_space.v4;

	list_for_each(pos, &bp->address_list) {
		addr = list_entry(pos, struct sockaddr_storage_list, list);
		len = sockaddr2sctp_addr(&addr->a, rawaddr);
		memcpy(addrparms.v, rawaddr.v, len);
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
			   int addrs_len, __u16 port, int priority)
{
	sctpParam_t rawaddr;
	sockaddr_storage_t addr;
	int retval = 0;
	int len;

	/* Convert the raw address to standard address format */
	while (addrs_len) {
		rawaddr.v = raw_addr_list;
		if (SCTP_PARAM_IPV4_ADDRESS==rawaddr.p->type
		    || SCTP_PARAM_IPV6_ADDRESS==rawaddr.p->type) {
			sctp_param2sockaddr(&addr, rawaddr, port);
			retval = sctp_add_bind_addr(bp, &addr, priority);
			if (retval) {
				/* Can't finish building the list, clean up. */
				sctp_bind_addr_clean(bp);
				break;
			}

			len = ntohs(rawaddr.p->length);
			addrs_len -= len;
			raw_addr_list += len;
		} else {
			/* Corrupted raw addr list! */
			retval = -EINVAL;
			sctp_bind_addr_clean(bp);
			break;
		}
	}

	return retval;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/* Does this contain a specified address? */
int sctp_bind_addr_has_addr(sctp_bind_addr_t *bp, const sockaddr_storage_t *addr)
{
	struct sockaddr_storage_list *laddr;
	list_t *pos;

	list_for_each(pos, &bp->address_list) {
		laddr = list_entry(pos, struct sockaddr_storage_list, list);
 		if (sctp_cmp_addr(&laddr->a, addr))
 			return 1;
	}

	return 0;
}

/* Copy out addresses from the global local address list. */
static int sctp_copy_one_addr(sctp_bind_addr_t *dest, sockaddr_storage_t *addr,
			      sctp_scope_t scope, int priority, int flags)
{
	sctp_protocol_t *proto = sctp_get_protocol();
	int error = 0;

	if (sctp_is_any(addr)) {
		error = sctp_copy_local_addr_list(proto, dest, scope,
						  priority, flags);
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
			error = sctp_add_bind_addr(dest, addr, priority);
	}

	return error;
}

/* Is addr one of the wildcards?  */
int sctp_is_any(const sockaddr_storage_t *addr)
{
	int retval = 0;

	switch (addr->sa.sa_family) {
	case AF_INET:
		if (INADDR_ANY == addr->v4.sin_addr.s_addr)
			retval = 1;
		break;

	case AF_INET6:
		SCTP_V6(
			if (IPV6_ADDR_ANY ==
			    sctp_ipv6_addr_type(&addr->v6.sin6_addr))
				retval = 1;
			);
		break;

	default:
		break;
	};

	return retval;
}

/* Is 'addr' valid for 'scope'?  */
int sctp_in_scope(const sockaddr_storage_t *addr, sctp_scope_t scope)
{
	sctp_scope_t addr_scope = sctp_scope(addr);

	switch (addr->sa.sa_family) {
	case AF_INET:
		/* According to the SCTP IPv4 address scoping document -
		 * <draft-stewart-tsvwg-sctp-ipv4-00.txt>, the scope has
		 * a heirarchy of 5 levels: 
		 * Level 0 - unusable SCTP addresses
		 * Level 1 - loopback address
		 * Level 2 - link-local addresses
		 * Level 3 - private addresses.
		 * Level 4 - global addresses
		 * For INIT and INIT-ACK address list, let L be the level of
		 * of requested destination address, sender and receiver
		 * SHOULD include all of its addresses with level greater
		 * than or equal to L.
		 */
		/* The unusable SCTP addresses will not be considered with
		 * any defined scopes.
		 */
		if (SCTP_SCOPE_UNUSABLE == addr_scope)
			return 0;

		/* Note that we are assuming that the scoping are the same
		 * for both IPv4 addresses and IPv6 addresses, i.e., if the
		 * scope is link local, both IPv4 link local addresses and
		 * IPv6 link local addresses would be treated as in the
		 * scope.  There is no filtering for IPv4 vs. IPv6 addresses
		 * based on scoping alone.
		 */
		if (addr_scope <= scope)
			return 1;
		break;

	case AF_INET6:
		/* FIXME:
		 * This is almost certainly wrong since scopes have an
		 * heirarchy.  I don't know what RFC to look at.
		 * There may be some guidance in the SCTP implementors
		 * guide (an Internet Draft as of October 2001).
		 *
		 * Further verification on the correctness of the IPv6
		 * scoping is needed.  According to the IPv6 scoping draft,
		 * the link local and site local address may require
		 * further scoping.
		 *
		 * Is the heirachy of the IPv6 scoping the same as what's
		 * defined for IPv4?
		 * If the same heirarchy indeed applies to both famiies,
		 * this function can be simplified with one set of code.
		 * (see the comments for IPv4 above)
		 */
		if (addr_scope <= scope)
			return 1;
		break;

	default:
		return 0;
	};

	return 0;
}

/********************************************************************
 * 3rd Level Abstractions
 ********************************************************************/

/* What is the scope of 'addr'?  */
sctp_scope_t sctp_scope(const sockaddr_storage_t *addr)
{
	sctp_scope_t retval = SCTP_SCOPE_GLOBAL;

	switch (addr->sa.sa_family) {
	case AF_INET:
		/* We are checking the loopback, private and other address
		 * scopes as defined in RFC 1918.
		 * The IPv4 scoping is based on the draft for SCTP IPv4
		 * scoping <draft-stewart-tsvwg-sctp-ipv4-00.txt>.
		 * The set of SCTP address scope hopefully can cover both
		 * types of addresses.
		 */

		/* Should IPv4 scoping be a sysctl configurable option
		 * so users can turn it off (default on) for certain
		 * unconventional networking environments?
		 */

		/* Check for unusable SCTP addresses. */
		if (IS_IPV4_UNUSABLE_ADDRESS(&addr->v4.sin_addr.s_addr)) {
			retval =  SCTP_SCOPE_UNUSABLE;
		} else if (LOOPBACK(addr->v4.sin_addr.s_addr)) {
			retval = SCTP_SCOPE_LOOPBACK;
		} else if (IS_IPV4_LINK_ADDRESS(&addr->v4.sin_addr.s_addr)) {
			retval = SCTP_SCOPE_LINK;
		} else if (IS_IPV4_PRIVATE_ADDRESS(&addr->v4.sin_addr.s_addr)) {
			retval = SCTP_SCOPE_PRIVATE;
		} else {
			retval = SCTP_SCOPE_GLOBAL;
		}
		break;

	case AF_INET6:
		{
			SCTP_V6(
				int v6scope;
				v6scope = ipv6_addr_scope((struct in6_addr *)
						 &addr->v6.sin6_addr);
				/* The IPv6 scope is really a set of bit
				 * fields. See IFA_* in <net/if_inet6.h>.
				 * Mapping them to the generic SCTP scope
				 * set is an attempt to have code
				 * consistencies with the IPv4 scoping.
				 */
				switch (v6scope) {
				case IFA_HOST:
					retval = SCTP_SCOPE_LOOPBACK;
					break;

				case IFA_LINK:
					retval = SCTP_SCOPE_LINK;
					break;

				case IFA_SITE:
					retval = SCTP_SCOPE_PRIVATE;
					break;

				default:
					retval = SCTP_SCOPE_GLOBAL;
					break;
				};
			);
			break;
		}

	default:
		retval = SCTP_SCOPE_GLOBAL;
		break;
	};

	return retval;
}

/* This function checks if the address is a valid address to be used for
 * SCTP.
 *
 * Output:
 * Return 0 - If the address is a non-unicast or an illegal address.
 * Return 1 - If the address is a unicast.
 */
int sctp_addr_is_valid(const sockaddr_storage_t *addr)
{
	unsigned short sa_family = addr->sa.sa_family;

	switch (sa_family) {
	case AF_INET:
		/* Is this a non-unicast address or a unusable SCTP address? */
		if (IS_IPV4_UNUSABLE_ADDRESS(&addr->v4.sin_addr.s_addr))
			return 0;
		break;

	case AF_INET6:
		SCTP_V6(
		{
			int ret = sctp_ipv6_addr_type(&addr->v6.sin6_addr);

			/* Is this a non-unicast address */
			if (!(ret & IPV6_ADDR_UNICAST))
				return 0;
			break;
		});

	default:
		return 0;
	};

	return 1;
}

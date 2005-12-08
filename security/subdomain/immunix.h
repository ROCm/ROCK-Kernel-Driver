/*
 *	Copyright (C) 2000, 2001, 2004, 2005 Novell/SUSE
 *
 *	Immunix AppArmor LSM
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 */

#ifndef _IMMUNIX_H
#define _IMMUNIX_H

/* start of system offsets */
#define POS_SD_FILE_MIN			0
#define POS_SD_MAY_EXEC			POS_SD_FILE_MIN
#define POS_SD_MAY_WRITE		(POS_SD_MAY_EXEC + 1)
#define POS_SD_MAY_READ			(POS_SD_MAY_WRITE + 1)
/* not used by Subdomain */
#define POS_SD_MAY_APPEND		(POS_SD_MAY_READ + 1)
/* end of system offsets */

#define POS_SD_MAY_LINK			(POS_SD_MAY_APPEND + 1)
#define POS_SD_EXEC_INHERIT		(POS_SD_MAY_LINK + 1)
#define POS_SD_EXEC_UNCONSTRAINED	(POS_SD_EXEC_INHERIT + 1)
#define POS_SD_EXEC_PROFILE		(POS_SD_EXEC_UNCONSTRAINED + 1)
#define POS_SD_FILE_MAX			POS_SD_EXEC_PROFILE

#define POS_SD_NET_MIN			(POS_SD_FILE_MAX + 1)
#define POS_SD_TCP_CONNECT		POS_SD_NET_MIN
#define POS_SD_TCP_ACCEPT		(POS_SD_TCP_CONNECT + 1)
#define POS_SD_TCP_CONNECTED		(POS_SD_TCP_ACCEPT + 1)
#define POS_SD_TCP_ACCEPTED		(POS_SD_TCP_CONNECTED + 1)
#define POS_SD_UDP_SEND			(POS_SD_TCP_ACCEPTED + 1)
#define POS_SD_UDP_RECEIVE		(POS_SD_UDP_SEND + 1)
#define POS_SD_NET_MAX			POS_SD_UDP_RECEIVE

/* logging only */
#define POS_SD_LOGTCP_SEND		(POS_SD_NET_MAX + 1)
#define POS_SD_LOGTCP_RECEIVE		(POS_SD_LOGTCP_SEND + 1)

/* Modeled after MAY_READ, MAY_WRITE, MAY_EXEC def'ns */
#define SD_MAY_EXEC			(0x01 << POS_SD_MAY_EXEC)
#define SD_MAY_WRITE			(0x01 << POS_SD_MAY_WRITE)
#define SD_MAY_READ			(0x01 << POS_SD_MAY_READ)
#define SD_MAY_LINK			(0x01 << POS_SD_MAY_LINK)
#define SD_EXEC_INHERIT			(0x01 << POS_SD_EXEC_INHERIT)
#define SD_EXEC_UNCONSTRAINED		(0x01 << POS_SD_EXEC_UNCONSTRAINED)
#define SD_EXEC_PROFILE			(0x01 << POS_SD_EXEC_PROFILE)
#define SD_EXEC_MODIFIERS(X)		(X & (SD_EXEC_INHERIT | \
					 SD_EXEC_UNCONSTRAINED | \
					 SD_EXEC_PROFILE))
/* Network subdomain extensions.  */
#define SD_TCP_CONNECT			(0x01 << POS_SD_TCP_CONNECT)
#define SD_TCP_ACCEPT			(0x01 << POS_SD_TCP_ACCEPT)
#define SD_TCP_CONNECTED		(0x01 << POS_SD_TCP_CONNECTED)
#define SD_TCP_ACCEPTED			(0x01 << POS_SD_TCP_ACCEPTED)
#define SD_UDP_SEND			(0x01 << POS_SD_UDP_SEND)
#define SD_UDP_RECEIVE			(0x01 << POS_SD_UDP_RECEIVE)

#define SD_LOGTCP_SEND			(0x01 << POS_SD_LOGTCP_SEND)
#define SD_LOGTCP_RECEIVE		(0x01 << POS_SD_LOGTCP_RECEIVE)

#define SD_HAT_SIZE	975		/* Maximum size of a subdomain
					 * ident (hat) */

enum entry_t {
	sd_entry_literal,
	sd_entry_tailglob,
	sd_entry_pattern,
	sd_entry_invalid
};

#endif				/* ! _IMMUNIX_H */

/*======================================================================*/

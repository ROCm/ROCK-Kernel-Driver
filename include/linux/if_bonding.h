/*
 * Bond several ethernet interfaces into a Cisco, running 'Etherchannel'.
 *
 * 
 * Portions are (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 * BUT, I'm the one who modified it for ethernet, so:
 * (c) Copyright 1999, Thomas Davis, tadavis@lbl.gov
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 * 
 */

#ifndef _LINUX_IF_BONDING_H
#define _LINUX_IF_BONDING_H

#define BOND_ENSLAVE     (SIOCDEVPRIVATE)
#define BOND_RELEASE     (SIOCDEVPRIVATE + 1)
#define BOND_SETHWADDR   (SIOCDEVPRIVATE + 2)

#endif /* _LINUX_BOND_H */

/*
 * Local variables:
 *  version-control: t
 *  kept-new-versions: 5
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */

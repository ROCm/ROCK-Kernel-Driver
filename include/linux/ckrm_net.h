/* ckrm_rc.h - Header file to be used by Resource controllers of CKRM
 *
 * Copyright (C) Vivek Kashyap , IBM Corp. 2004
 * 
 * Provides data structures, macros and kernel API of CKRM for 
 * resource controllers.
 *
 * Latest version, more details at http://ckrm.sf.net
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef _LINUX_CKRM_NET_H
#define _LINUX_CKRM_NET_H

struct ckrm_sock_class;

struct ckrm_net_struct {
	int 		 ns_type;                    // type of net class
	struct sock     *ns_sk;         // pointer to socket
	pid_t            ns_tgid;       // real process id
	pid_t            ns_pid;        // calling thread's pid
	int              ns_family;     // IPPROTO_IPV4 || IPPROTO_IPV6
					// Currently only IPV4 is supported
	union {
		__u32   ns_dipv4;       // V4 listener's address
	} ns_daddr;
	__u16 		ns_dport;       // listener's port
	__u16 ns_sport;                 // sender's port
	atomic_t ns_refcnt;
	struct ckrm_sock_class 	*core;		
	struct list_head       ckrm_link;
};

#define ns_daddrv4     ns_daddr.ns_dipv4

#endif

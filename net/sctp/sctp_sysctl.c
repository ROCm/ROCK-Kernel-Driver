/* SCTP kernel reference Implementation 
 * Copyright (c) 2002 International Business Machines Corp.
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * $Header: /cvsroot/lksctp/lksctp/sctp_cvs/net/sctp/sctp_sysctl.c,v 1.2 2002/07/12 14:50:25 jgrimm Exp $
 * 
 * Sysctl related interfaces for SCTP. 
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
 *    Mingqin Liu           <liuming@us.ibm.com>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */
static char *cvs_id __attribute__ ((unused)) = "$Id: sctp_sysctl.c,v 1.2 2002/07/12 14:50:25 jgrimm Exp $";

#include <net/sctp/sctp_structs.h>
#include <linux/sysctl.h>

extern sctp_protocol_t sctp_proto;

static ctl_table sctp_table[] = {
	{NET_SCTP_RTO_INITIAL, "rto_initial",
	 &sctp_proto.rto_initial, sizeof(int), 0644, NULL, 
	 &proc_dointvec_jiffies, &sysctl_jiffies},
	{NET_SCTP_RTO_MIN, "rto_min",
	 &sctp_proto.rto_min, sizeof(int), 0644, NULL, 
	 &proc_dointvec_jiffies, &sysctl_jiffies},
	{NET_SCTP_RTO_MAX, "rto_max",
	 &sctp_proto.rto_max, sizeof(int), 0644, NULL, 
	 &proc_dointvec_jiffies, &sysctl_jiffies},
	{NET_SCTP_VALID_COOKIE_LIFE, "valid_cookie_life",
	 &sctp_proto.valid_cookie_life, sizeof(int), 0644, NULL, 
	 &proc_dointvec_jiffies, &sysctl_jiffies},
	{NET_SCTP_MAX_BURST, "max_burst",
	 &sctp_proto.max_burst, sizeof(int), 0644, NULL, 
	 &proc_dointvec},
	{NET_SCTP_ASSOCIATION_MAX_RETRANS, "association_max_retrans",
	 &sctp_proto.max_retrans_association, sizeof(int), 0644, NULL, 
	 &proc_dointvec},
	{NET_SCTP_PATH_MAX_RETRANS, "path_max_retrans",
	 &sctp_proto.max_retrans_path, sizeof(int), 0644, NULL, 
	 &proc_dointvec},
	{NET_SCTP_MAX_INIT_RETRANSMITS, "max_init_retransmits",
	 &sctp_proto.max_retrans_init, sizeof(int), 0644, NULL, 
	 &proc_dointvec},
	{NET_SCTP_HB_INTERVAL, "hb_interval",
	 &sctp_proto.hb_interval, sizeof(int), 0644, NULL, 
	 &proc_dointvec_jiffies, &sysctl_jiffies},
	{NET_SCTP_RTO_ALPHA, "rto_alpha_exp_divisor",
	 &sctp_proto.rto_alpha, sizeof(int), 0644, NULL, 
	 &proc_dointvec},
	{NET_SCTP_RTO_BETA, "rto_beta_exp_divisor",
	 &sctp_proto.rto_beta, sizeof(int), 0644, NULL, 
	 &proc_dointvec},
	{0}
};

static ctl_table sctp_net_table[] = {
	{NET_SCTP, "sctp", NULL, 0, 0555, sctp_table},
	{0}
};

static ctl_table sctp_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, sctp_net_table},
	{0}
};

static struct ctl_table_header * sctp_sysctl_header;

/* Sysctl registration. */
void sctp_sysctl_register(void)
{
	sctp_sysctl_header = register_sysctl_table(sctp_root_table, 0);

} /* sctp_sysctl_register() */

/* Sysctl deregistration. */
void sctp_sysctl_unregister(void)
{
	unregister_sysctl_table(sctp_sysctl_header);

} /* sctp_sysctl_unregister() */


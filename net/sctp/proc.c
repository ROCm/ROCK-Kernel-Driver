/* SCTP kernel reference Implementation
 * Copyright (c) 2003 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
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
 *    Sridhar Samudrala <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <net/sctp/sctp.h>

static char *sctp_snmp_list[] = {
#define SCTP_SNMP_ENTRY(x) #x 
	SCTP_SNMP_ENTRY(SctpCurrEstab),
	SCTP_SNMP_ENTRY(SctpActiveEstabs),
	SCTP_SNMP_ENTRY(SctpPassiveEstabs),
	SCTP_SNMP_ENTRY(SctpAborteds),
	SCTP_SNMP_ENTRY(SctpShutdowns),
	SCTP_SNMP_ENTRY(SctpOutOfBlues),
	SCTP_SNMP_ENTRY(SctpChecksumErrors),
	SCTP_SNMP_ENTRY(SctpOutCtrlChunks),
	SCTP_SNMP_ENTRY(SctpOutOrderChunks),
	SCTP_SNMP_ENTRY(SctpOutUnorderChunks),
	SCTP_SNMP_ENTRY(SctpInCtrlChunks),
	SCTP_SNMP_ENTRY(SctpInOrderChunks),
	SCTP_SNMP_ENTRY(SctpInUnorderChunks),
	SCTP_SNMP_ENTRY(SctpFragUsrMsgs),
	SCTP_SNMP_ENTRY(SctpReasmUsrMsgs),
	SCTP_SNMP_ENTRY(SctpOutSCTPPacks),
	SCTP_SNMP_ENTRY(SctpInSCTPPacks),
#undef SCTP_SNMP_ENTRY
};

/* Return the current value of a particular entry in the mib by adding its
 * per cpu counters.
 */ 
static unsigned long
fold_field(void *mib[], int nr)
{
	unsigned long res = 0;
	int i;

	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;
		res +=
		    *((unsigned long *) (((void *) per_cpu_ptr(mib[0], i)) +
					 sizeof (unsigned long) * nr));
		res +=
		    *((unsigned long *) (((void *) per_cpu_ptr(mib[1], i)) +
					 sizeof (unsigned long) * nr));
	}
	return res;
}

/* Display sctp snmp mib statistics(/proc/net/sctp/snmp). */
static int sctp_snmp_seq_show(struct seq_file *seq, void *v)
{
	int i;

	for (i = 0; i < sizeof(sctp_snmp_list) / sizeof(char *); i++)
		seq_printf(seq, "%-32s\t%ld\n", sctp_snmp_list[i],
			   fold_field((void **)sctp_statistics, i));

	return 0;
}

/* Initialize the seq file operations for 'snmp' object. */
static int sctp_snmp_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, sctp_snmp_seq_show, NULL);
}

static struct file_operations sctp_snmp_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = sctp_snmp_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

/* Set up the proc fs entry for 'snmp' object. */
int __init sctp_snmp_proc_init(void)
{
	struct proc_dir_entry *p;

	p = create_proc_entry("snmp", S_IRUGO, proc_net_sctp);
	if (!p)
		return -ENOMEM;

	p->proc_fops = &sctp_snmp_seq_fops;

	return 0;
}

/* Cleanup the proc fs entry for 'snmp' object. */
void sctp_snmp_proc_exit(void)
{
	remove_proc_entry("snmp", proc_net_sctp);
}

/*******************************************************************************

This software program is available to you under a choice of one of two 
licenses. You may choose to be licensed under either the GNU General Public 
License 2.0, June 1991, available at http://www.fsf.org/copyleft/gpl.html, 
or the Intel BSD + Patent License, the text of which follows:

Recipient has requested a license and Intel Corporation ("Intel") is willing
to grant a license for the software entitled Linux Base Driver for the 
Intel(R) PRO/100 Family of Adapters (e100) (the "Software") being provided 
by Intel Corporation. The following definitions apply to this license:

"Licensed Patents" means patent claims licensable by Intel Corporation which 
are necessarily infringed by the use of sale of the Software alone or when 
combined with the operating system referred to below.

"Recipient" means the party to whom Intel delivers this Software.

"Licensee" means Recipient and those third parties that receive a license to 
any operating system available under the GNU General Public License 2.0 or 
later.

Copyright (c) 1999 - 2002 Intel Corporation.
All rights reserved.

The license is provided to Recipient and Recipient's Licensees under the 
following terms.

Redistribution and use in source and binary forms of the Software, with or 
without modification, are permitted provided that the following conditions 
are met:

Redistributions of source code of the Software may retain the above 
copyright notice, this list of conditions and the following disclaimer.

Redistributions in binary form of the Software may reproduce the above 
copyright notice, this list of conditions and the following disclaimer in 
the documentation and/or materials provided with the distribution.

Neither the name of Intel Corporation nor the names of its contributors 
shall be used to endorse or promote products derived from this Software 
without specific prior written permission.

Intel hereby grants Recipient and Licensees a non-exclusive, worldwide, 
royalty-free patent license under Licensed Patents to make, use, sell, offer 
to sell, import and otherwise transfer the Software, if any, in source code 
and object code form. This license shall include changes to the Software 
that are error corrections or other minor changes to the Software that do 
not add functionality or features when the Software is incorporated in any 
version of an operating system that has been distributed under the GNU 
General Public License 2.0 or later. This patent license shall apply to the 
combination of the Software and any operating system licensed under the GNU 
General Public License 2.0 or later if, at the time Intel provides the 
Software to Recipient, such addition of the Software to the then publicly 
available versions of such operating systems available under the GNU General 
Public License 2.0 or later (whether in gold, beta or alpha form) causes 
such combination to be covered by the Licensed Patents. The patent license 
shall not apply to any other combinations which include the Software. NO 
hardware per se is licensed hereunder.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
IMPLIED WARRANTIES OF MECHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR IT CONTRIBUTORS BE LIABLE FOR ANY 
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
ANY LOSS OF USE; DATA, OR PROFITS; OR BUSINESS INTERUPTION) HOWEVER CAUSED 
AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR 
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/**********************************************************************
*                                                                       *
* INTEL CORPORATION                                                     *
*                                                                       *
* This software is supplied under the terms of the license included     *
* above.  All use of this driver must be in accordance with the terms   *
* of that license.                                                      *
*                                                                       *
* Module Name:  e100_proc.c                                             *
*                                                                       *
* Abstract:     Functions to handle the proc file system.               *
*               Create the proc directories and files and run read and  *
*               write requests from the user                            *
*                                                                       *
* Environment:  This file is intended to be specific to the Linux       *
*               operating system.                                       *
*                                                                       *
**********************************************************************/

#include <linux/config.h>

#ifdef CONFIG_PROC_FS
#include "e100.h"
/* MDI sleep time is at least 50 ms, in jiffies */
#define MDI_SLEEP_TIME ((HZ / 20) + 1)
/***************************************************************************/
/*       /proc File System Interaface Support Functions                    */
/***************************************************************************/

static struct proc_dir_entry *adapters_proc_dir = 0;

/* externs from e100_main.c */
extern char e100_short_driver_name[];
extern char e100_driver_version[];
extern struct net_device_stats *e100_get_stats(struct net_device *dev);
extern char *e100_get_brand_msg(struct e100_private *bdp);
extern void e100_mdi_write(struct e100_private *, u32, u32, u16);

static void e100_proc_cleanup(void);
static unsigned char e100_init_proc_dir(void);

#define ADAPTERS_PROC_DIR "PRO_LAN_Adapters"
#define WRITE_BUF_MAX_LEN 20	
#define READ_BUF_MAX_LEN  256
#define E100_PE_LEN       25

#define bdp_drv_off(off) (unsigned long)(offsetof(struct e100_private, drv_stats.off))
#define bdp_prm_off(off) (unsigned long)(offsetof(struct e100_private, params.off))

typedef struct _e100_proc_entry {
	char *name;
	read_proc_t *read_proc;
	write_proc_t *write_proc;
	unsigned long offset;	/* offset into bdp. ~0 means no value, pass NULL. */
} e100_proc_entry;

static int
generic_read(char *page, char **start, off_t off, int count, int *eof, int len)
{
	if (len <= off + count)
		*eof = 1;

	*start = page + off;
	len -= off;
	if (len > count)
		len = count;

	if (len < 0)
		len = 0;

	return len;
}

static int
read_ulong(char *page, char **start, off_t off,
	   int count, int *eof, unsigned long l)
{
	int len;

	len = sprintf(page, "%lu\n", l);

	return generic_read(page, start, off, count, eof, len);
}

static int
read_gen_ulong(char *page, char **start, off_t off,
	       int count, int *eof, void *data)
{
	unsigned long val = 0;

	if (data)
		val = *((unsigned long *) data);

	return read_ulong(page, start, off, count, eof, val);
}

static int
read_hwaddr(char *page, char **start, off_t off,
	    int count, int *eof, unsigned char *hwaddr)
{
	int len;

	len = sprintf(page, "%02X:%02X:%02X:%02X:%02X:%02X\n",
		      hwaddr[0], hwaddr[1], hwaddr[2],
		      hwaddr[3], hwaddr[4], hwaddr[5]);

	return generic_read(page, start, off, count, eof, len);
}

static int
read_descr(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct e100_private *bdp = data;
	int len;

	len = sprintf(page, "%s\n", bdp->id_string);

	return generic_read(page, start, off, count, eof, len);
}

static int
read_permanent_hwaddr(char *page, char **start, off_t off,
		      int count, int *eof, void *data)
{
	struct e100_private *bdp = data;
	unsigned char *hwaddr = bdp->perm_node_address;

	return read_hwaddr(page, start, off, count, eof, hwaddr);
}

static int
read_part_number(char *page, char **start, off_t off,
		 int count, int *eof, void *data)
{
	struct e100_private *bdp = data;
	int len;

	len = sprintf(page, "%06lx-%03x\n",
		      (unsigned long) (bdp->pwa_no >> 8),
		      (unsigned int) (bdp->pwa_no & 0xFF));

	return generic_read(page, start, off, count, eof, len);
}

static void
set_led(struct e100_private *bdp, u16 led_mdi_op)
{
	e100_mdi_write(bdp, PHY_82555_LED_SWITCH_CONTROL,
		       bdp->phy_addr, led_mdi_op);

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(MDI_SLEEP_TIME);

	/* turn led ownership to the chip */
	e100_mdi_write(bdp, PHY_82555_LED_SWITCH_CONTROL,
		       bdp->phy_addr, PHY_82555_LED_NORMAL_CONTROL);
}

static int
write_blink_led_timer(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	struct e100_private *bdp = data;
	char s_blink_op[WRITE_BUF_MAX_LEN + 1];
	char *res;
	unsigned long i_blink_op;

	if (!buffer)
		return -EINVAL;

	if (count > WRITE_BUF_MAX_LEN) {
		count = WRITE_BUF_MAX_LEN;
	}
	if (copy_from_user(s_blink_op, buffer, count))
		return -EFAULT;
	s_blink_op[count] = '\0';
	i_blink_op = simple_strtoul(s_blink_op, &res, 0);
	if (res == s_blink_op) {
		return -EINVAL;
	}

	switch (i_blink_op) {

	case LED_OFF:
		set_led(bdp, PHY_82555_LED_OFF);
		break;
	case LED_ON:
		if (bdp->rev_id >= D101MA_REV_ID)
			set_led(bdp, PHY_82555_LED_ON_559);
		else
			set_led(bdp, PHY_82555_LED_ON_PRE_559);

		break;
	default:
		return -EINVAL;
	}

	return count;
}

static e100_proc_entry e100_proc_list[] = {
	{"Description",           read_descr,            0, 0},
	{"Permanent_HWaddr",      read_permanent_hwaddr, 0, 0},
	{"Part_Number",           read_part_number,      0, 0},
	{"\n",},
	{"Rx_TCP_Checksum_Good",  read_gen_ulong, 0, ~0},
	{"Rx_TCP_Checksum_Bad",   read_gen_ulong, 0, ~0},
	{"Tx_TCP_Checksum_Good",  read_gen_ulong, 0, ~0},
	{"Tx_TCP_Checksum_Bad",   read_gen_ulong, 0, ~0},
	{"\n",},
	{"Tx_Abort_Late_Coll",    read_gen_ulong, 0, bdp_drv_off(tx_late_col)},
	{"Tx_Deferred_Ok",        read_gen_ulong, 0, bdp_drv_off(tx_ok_defrd)},
	{"Tx_Single_Coll_Ok",     read_gen_ulong, 0, bdp_drv_off(tx_one_retry)},
	{"Tx_Multi_Coll_Ok",      read_gen_ulong, 0, bdp_drv_off(tx_mt_one_retry)},
	{"Rx_Long_Length_Errors", read_gen_ulong, 0, ~0},
	{"\n",},
	{"Tx_Flow_Control_Pause", read_gen_ulong, 0, bdp_drv_off(xmt_fc_pkts)},
	{"Rx_Flow_Control_Pause", read_gen_ulong, 0, bdp_drv_off(rcv_fc_pkts)},
	{"Rx_Flow_Control_Unsup", read_gen_ulong, 0, bdp_drv_off(rcv_fc_unsupported)},
	{"\n",},
	{"Tx_TCO_Packets",        read_gen_ulong, 0, bdp_drv_off(xmt_tco_pkts)},
	{"Rx_TCO_Packets",        read_gen_ulong, 0, bdp_drv_off(rcv_tco_pkts)},
	{"\n",},
	{"Rx_Interrupt_Packets",  read_gen_ulong, 0, bdp_drv_off(rx_intr_pkts)},
	{"Rx_Polling_Packets",    read_gen_ulong, 0, bdp_drv_off(rx_tasklet_pkts)},
	{"Polling_Interrupt_Switch", read_gen_ulong, 0, bdp_drv_off(poll_intr_switch)},
	{"Identify_Adapter", 0, write_blink_led_timer, 0},
	{"", 0, 0, 0}
};

static int
read_info(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct e100_private *bdp = data;
	e100_proc_entry *pe;
	int tmp;
	void *val;
	int len = 0;

	for (pe = e100_proc_list; pe->name[0]; pe++) {
		if (pe->name[0] == '\n') {
			len += sprintf(page + len, "\n");
			continue;
		}

		if (pe->read_proc) {
			if ((len + READ_BUF_MAX_LEN + E100_PE_LEN + 1) >=
			    PAGE_SIZE)
				break;

			if (pe->offset != ~0)
				val = ((char *) bdp) + pe->offset;
			else
				val = NULL;

			len += sprintf(page + len, "%-"
				       __MODULE_STRING(E100_PE_LEN)
				       "s ", pe->name);
			len += pe->read_proc(page + len, start, 0,
					     READ_BUF_MAX_LEN + 1, &tmp, val);
		}
	}

	return generic_read(page, start, off, count, eof, len);
}

static struct proc_dir_entry * __devinit
create_proc_rw(char *name, void *data, struct proc_dir_entry *parent,
	       read_proc_t * read_proc, write_proc_t * write_proc)
{
	struct proc_dir_entry *pdep;
	mode_t mode = S_IFREG;

	if (write_proc) {
		mode |= S_IWUSR;
		if (read_proc) {
			mode |= S_IRUSR;
		}

	} else if (read_proc) {
		mode |= S_IRUGO;
	}

	if (!(pdep = create_proc_entry(name, mode, parent)))
		return NULL;

	pdep->read_proc = read_proc;
	pdep->write_proc = write_proc;
	pdep->data = data;
	return pdep;
}

void
e100_remove_proc_subdir(struct e100_private *bdp)
{
	e100_proc_entry *pe;
	char info[256];
	int len;

	/* If our root /proc dir was not created, there is nothing to remove */
	if (adapters_proc_dir == NULL) {
		return;
	}

	len = strlen(bdp->device->name);
	strncpy(info, bdp->device->name, sizeof (info));
	strncat(info + len, ".info", sizeof (info) - len);

	if (bdp->proc_parent) {
		for (pe = e100_proc_list; pe->name[0]; pe++) {
			if (pe->name[0] == '\n')
				continue;

			remove_proc_entry(pe->name, bdp->proc_parent);
		}

		remove_proc_entry(bdp->device->name, adapters_proc_dir);
		bdp->proc_parent = NULL;
	}

	remove_proc_entry(info, adapters_proc_dir);

	/* try to remove the main /proc dir, if it's empty */
	e100_proc_cleanup();
}

int __devinit
e100_create_proc_subdir(struct e100_private *bdp)
{
	struct proc_dir_entry *dev_dir;
	e100_proc_entry *pe;
	char info[256];
	int len;
	void *data;

	/* create the main /proc dir if needed */
	if (!adapters_proc_dir) {
		if (!e100_init_proc_dir())
			return -ENOMEM;
	}

	strncpy(info, bdp->device->name, sizeof (info));
	len = strlen(info);
	strncat(info + len, ".info", sizeof (info) - len);

	/* info */
	if (!(create_proc_rw(info, bdp, adapters_proc_dir, read_info, 0))) {
		e100_proc_cleanup();
		return -ENOMEM;
	}

	dev_dir = create_proc_entry(bdp->device->name, S_IFDIR,
				    adapters_proc_dir);
	bdp->proc_parent = dev_dir;

	if (!dev_dir) {
		e100_remove_proc_subdir(bdp);
		return -ENOMEM;
	}

	for (pe = e100_proc_list; pe->name[0]; pe++) {
		if (pe->name[0] == '\n')
			continue;

		if (pe->offset != ~0)
			data = ((char *) bdp) + pe->offset;
		else
			data = NULL;

		if (!(create_proc_rw(pe->name, data, dev_dir,
				     pe->read_proc, pe->write_proc))) {
			e100_remove_proc_subdir(bdp);
			return -ENOMEM;
		}
	}

	return 0;
}

/****************************************************************************
 * Name:          e100_init_proc_dir
 *
 * Description:   This routine creates the top-level /proc directory for the
 *                driver in /proc/net
 *
 * Arguments:     none
 *
 * Returns:       true on success, false on fail
 *
 ***************************************************************************/
static unsigned char
e100_init_proc_dir(void)
{
	int len;

	/* first check if adapters_proc_dir already exists */
	len = strlen(ADAPTERS_PROC_DIR);
	for (adapters_proc_dir = proc_net->subdir;
	     adapters_proc_dir; adapters_proc_dir = adapters_proc_dir->next) {

		if ((adapters_proc_dir->namelen == len) &&
		    (!memcmp(adapters_proc_dir->name, ADAPTERS_PROC_DIR, len)))
			break;
	}

	if (!adapters_proc_dir)
		adapters_proc_dir =
			create_proc_entry(ADAPTERS_PROC_DIR, S_IFDIR, proc_net);

	if (!adapters_proc_dir)
		return false;

	return true;
}

/****************************************************************************
 * Name:          e100_proc_cleanup
 *
 * Description:   This routine clears the top-level /proc directory, if empty.
 *
 * Arguments:     none
 *
 * Returns:       none
 *
 ***************************************************************************/
static void
e100_proc_cleanup(void)
{
	struct proc_dir_entry *de;

	if (adapters_proc_dir == NULL) {
		return;
	}

	/* check if subdir list is empty before removing adapters_proc_dir */
	for (de = adapters_proc_dir->subdir; de; de = de->next) {
		/* ignore . and .. */
		if (*(de->name) != '.')
			break;
	}

	if (de)
		return;

	remove_proc_entry(ADAPTERS_PROC_DIR, proc_net);
	adapters_proc_dir = NULL;
}

#endif /* CONFIG_PROC_FS */

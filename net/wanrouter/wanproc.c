/*****************************************************************************
* wanproc.c	WAN Router Module. /proc filesystem interface.
*
*		This module is completely hardware-independent and provides
*		access to the router using Linux /proc filesystem.
*
* Author: 	Gideon Hack	
*
* Copyright:	(c) 1995-1999 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Jun 02, 1999  Gideon Hack	Updates for Linux 2.2.X kernels.
* Jun 29, 1997	Alan Cox	Merged with 1.0.3 vendor code
* Jan 29, 1997	Gene Kozin	v1.0.1. Implemented /proc read routines
* Jan 30, 1997	Alan Cox	Hacked around for 2.1
* Dec 13, 1996	Gene Kozin	Initial version (based on Sangoma's WANPIPE)
*****************************************************************************/

#include <linux/version.h>
#include <linux/config.h>
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/kernel.h>
#include <linux/malloc.h>	/* kmalloc(), kfree() */
#include <linux/mm.h>		/* verify_area(), etc. */
#include <linux/string.h>	/* inline mem*, str* functions */
#include <linux/init.h>		/* __init et al. */
#include <asm/segment.h>	/* kernel <-> user copy */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <asm/uaccess.h>	/* copy_to_user */
#include <asm/io.h>
#include <linux/wanrouter.h>	/* WAN router API definitions */


/****** Defines and Macros **************************************************/

#define PROC_STATS_FORMAT "%30s: %12lu\n"

#ifndef	min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef	max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

#define	PROC_BUFSZ	4000	/* buffer size for printing proc info */


/****** Data Types **********************************************************/

typedef struct wan_stat_entry
{
	struct wan_stat_entry *next;
	char *description;		/* description string */
	void *data;			/* -> data */
	unsigned data_type;		/* data type */
} wan_stat_entry_t;

/****** Function Prototypes *************************************************/

#ifdef CONFIG_PROC_FS

/* Proc filesystem interface */
static int router_proc_perms(struct inode *, int);
static ssize_t router_proc_read(struct file* file, char* buf, size_t count, 					loff_t *ppos);

/* Methods for preparing data for reading proc entries */

static int config_get_info(char* buf, char** start, off_t offs, int len);
static int status_get_info(char* buf, char** start, off_t offs, int len);
static int wandev_get_info(char* buf, char** start, off_t offs, int len);

/* Miscellaneous */

/*
 *	Structures for interfacing with the /proc filesystem.
 *	Router creates its own directory /proc/net/router with the folowing
 *	entries:
 *	config		device configuration
 *	status		global device statistics
 *	<device>	entry for each WAN device
 */

/*
 *	Generic /proc/net/router/<file> file and inode operations 
 */
static struct file_operations router_fops =
{
	read:		router_proc_read,
};

static struct inode_operations router_inode =
{
	permission:	router_proc_perms,
};

/*
 *	/proc/net/router/<device> file operations
 */

static struct file_operations wandev_fops =
{
	read:		router_proc_read,
	ioctl:		wanrouter_ioctl,
};

/*
 *	/proc/net/router 
 */

static struct proc_dir_entry *proc_router;

/* Strings */
static char conf_hdr[] =
	"Device name    | port |IRQ|DMA| mem.addr |mem.size|"
	"option1|option2|option3|option4\n";
	
static char stat_hdr[] =
	"Device name    |station|interface|clocking|baud rate| MTU |ndev"
	"|link state\n";


/*
 *	Interface functions
 */

/*
 *	Initialize router proc interface.
 */

int __init wanrouter_proc_init (void)
{
	struct proc_dir_entry *p;
	proc_router = proc_mkdir(ROUTER_NAME, proc_net);
	if (!proc_router)
		goto fail;

	p = create_proc_entry("config",0,proc_router);
	if (!p)
		goto fail_config;
	p->proc_fops = &router_fops;
	p->proc_iops = &router_inode;
	p->get_info = config_get_info;
	p = create_proc_entry("status",0,proc_router);
	if (!p)
		goto fail_stat;
	p->proc_fops = &router_fops;
	p->proc_iops = &router_inode;
	p->get_info = status_get_info;
	return 0;
fail_stat:
	remove_proc_entry("config", proc_router);
fail_config:
	remove_proc_entry(ROUTER_NAME, proc_net);
fail:
	return -ENOMEM;
}

/*
 *	Clean up router proc interface.
 */

void wanrouter_proc_cleanup (void)
{
	remove_proc_entry("config", proc_router);
	remove_proc_entry("status", proc_router);
	remove_proc_entry(ROUTER_NAME,proc_net);
}

/*
 *	Add directory entry for WAN device.
 */

int wanrouter_proc_add (wan_device_t* wandev)
{
	if (wandev->magic != ROUTER_MAGIC)
		return -EINVAL;
		
	wandev->dent = create_proc_entry(wandev->name, 0, proc_router);
	if (!wandev->dent)
		return -ENOMEM;
	wandev->dent->proc_fops	= &wandev_fops;
	wandev->dent->proc_iops	= &router_inode;
	wandev->dent->get_info	= wandev_get_info;
	wandev->dent->data	= wandev;
	return 0;
}

/*
 *	Delete directory entry for WAN device.
 */
 
int wanrouter_proc_delete(wan_device_t* wandev)
{
	if (wandev->magic != ROUTER_MAGIC)
		return -EINVAL;
	remove_proc_entry(wandev->name, proc_router);
	return 0;
}

/****** Proc filesystem entry points ****************************************/

/*
 *	Verify access rights.
 */

static int router_proc_perms (struct inode* inode, int op)
{
	return 0;
}

/*
 *	Read router proc directory entry.
 *	This is universal routine for reading all entries in /proc/net/wanrouter
 *	directory.  Each directory entry contains a pointer to the 'method' for
 *	preparing data for that entry.
 *	o verify arguments
 *	o allocate kernel buffer
 *	o call get_info() to prepare data
 *	o copy data to user space
 *	o release kernel buffer
 *
 *	Return:	number of bytes copied to user space (0, if no data)
 *		<0	error
 */

static ssize_t router_proc_read(struct file* file, char* buf, size_t count,
				loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct proc_dir_entry* dent;
	char* page;
	int pos, offs, len;

	if (count <= 0)
		return 0;
		
	dent = inode->u.generic_ip;
	if ((dent == NULL) || (dent->get_info == NULL))
		return 0;
		
	page = kmalloc(PROC_BUFSZ, GFP_KERNEL);
	if (page == NULL)
		return -ENOBUFS;
		
	pos = dent->get_info(page, dent->data, 0, 0);
	offs = file->f_pos;
	if (offs < pos) {
		len = min(pos - offs, count);
		if(copy_to_user(buf, (page + offs), len))
			return -EFAULT;
		file->f_pos += len;
	}
	else
		len = 0;
	kfree(page);
	return len;
}

/*
 *	Prepare data for reading 'Config' entry.
 *	Return length of data.
 */

static int config_get_info(char* buf, char** start, off_t offs, int len)
{
	int cnt = sizeof(conf_hdr) - 1;
	wan_device_t* wandev;
	strcpy(buf, conf_hdr);
	for (wandev = router_devlist;
	     wandev && (cnt < (PROC_BUFSZ - 120));
	     wandev = wandev->next) {
		if (wandev->state) cnt += sprintf(&buf[cnt],
			"%-15s|0x%-4X|%3u|%3u| 0x%-8lX |0x%-6X|%7u|%7u|%7u|%7u\n",
			wandev->name,
			wandev->ioport,
			wandev->irq,
			wandev->dma,
			wandev->maddr,
			wandev->msize,
			wandev->hw_opt[0],
			wandev->hw_opt[1],
			wandev->hw_opt[2],
			wandev->hw_opt[3]);
	}

	return cnt;
}

/*
 *	Prepare data for reading 'Status' entry.
 *	Return length of data.
 */

static int status_get_info(char* buf, char** start, off_t offs, int len)
{
	int cnt = 0;
	wan_device_t* wandev;

	cnt += sprintf(&buf[cnt], "\nSTATUS FOR PORT 0\n\n");
	strcpy(&buf[cnt], stat_hdr);
	cnt += sizeof(stat_hdr) - 1;

	for (wandev = router_devlist;
	     wandev && (cnt < (PROC_BUFSZ - 80));
	     wandev = wandev->next) {
		if (!wandev->state) continue;
		cnt += sprintf(&buf[cnt],
			"%-15s|%-7s|%-9s|%-8s|%9u|%5u|%3u |",
			wandev->name,
			wandev->station ? " DCE" : " DTE",
			wandev->interface ? " V.35" : " RS-232",
			wandev->clocking ? "internal" : "external",
			wandev->bps,
			wandev->mtu,
			wandev->ndev);

		switch (wandev->state) {

		case WAN_UNCONFIGURED:
			cnt += sprintf(&buf[cnt], "%-12s\n", "unconfigured");
			break;

		case WAN_DISCONNECTED:
			cnt += sprintf(&buf[cnt], "%-12s\n", "disconnected");
			break;

		case WAN_CONNECTING:
			cnt += sprintf(&buf[cnt], "%-12s\n", "connecting");
			break;

		case WAN_CONNECTED:
			cnt += sprintf(&buf[cnt], "%-12s\n", "connected");
			break;

		default:
			cnt += sprintf(&buf[cnt], "%-12s\n", "invalid");
			break;
		}
	}
	return cnt;
}

/*
 *	Prepare data for reading <device> entry.
 *	Return length of data.
 *
 *	On entry, the 'start' argument will contain a pointer to WAN device
 *	data space.
 */

static int wandev_get_info(char* buf, char** start, off_t offs, int len)
{
	wan_device_t* wandev = (void*)start;
	int cnt = 0;
	int rslt = 0;

	if ((wandev == NULL) || (wandev->magic != ROUTER_MAGIC))
		return 0;
	if (!wandev->state)
		return sprintf(&buf[cnt], "device is not configured!\n");

	/* Update device statistics */
	if (wandev->update) {

		rslt = wandev->update(wandev);
		if(rslt) {
			switch (rslt) {
			case -EAGAIN:
				return sprintf(&buf[cnt], "Device is busy!\n");

			default:
				return sprintf(&buf[cnt],
					"Device is not configured!\n");
			}
		}
	}

        cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "total packets received", wandev->stats.rx_packets);
	cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "total packets transmitted", wandev->stats.tx_packets);
        cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "total bytes received", wandev->stats.rx_bytes);
        cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "total bytes transmitted", wandev->stats.tx_bytes);
        cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "bad packets received", wandev->stats.rx_errors);
       	cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "packet transmit problems", wandev->stats.tx_errors);
	cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "received frames dropped", wandev->stats.rx_dropped);
        cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "transmit frames dropped", wandev->stats.tx_dropped);
	cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "multicast packets received", wandev->stats.multicast);
        cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "transmit collisions", wandev->stats.collisions);
        cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "receive length errors", wandev->stats.rx_length_errors);
	cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
		"receiver overrun errors", wandev->stats.rx_over_errors);
	cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
		"CRC errors", wandev->stats.rx_crc_errors);
	cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
		"frame format errors (aborts)", wandev->stats.rx_frame_errors);
        cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
                "receiver fifo overrun", wandev->stats.rx_fifo_errors);
	cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
		"receiver missed packet", wandev->stats.rx_missed_errors);
	cnt += sprintf(&buf[cnt], PROC_STATS_FORMAT,
		"aborted frames transmitted", wandev->stats.tx_aborted_errors);
  	return cnt;
}

/*
 *	End
 */
 
#else

/*
 *	No /proc - output stubs
 */
 
int __init wanrouter_proc_init(void)
{
	return 0;
}

void wanrouter_proc_cleanup(void)
{
	return;
}

int wanrouter_proc_add(wan_device_t *wandev)
{
	return 0;
}

int wanrouter_proc_delete(wan_device_t *wandev)
{
	return 0;
}

#endif

/*
 *	End
 */
 

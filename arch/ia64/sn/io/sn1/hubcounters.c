/* $Id:$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 - 2001 Silicon Graphics, Inc.
 * All rights reserved.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/types.h>
#include <asm/sn/io.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/iograph.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/router.h>
#include <asm/sn/snconfig.h>
#include <asm/sn/slotnum.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/sndrv.h>

extern void hubni_error_handler(char *, int); /* huberror.c */

static int hubstats_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
struct file_operations hub_mon_fops = {
        .ioctl =        hubstats_ioctl,
};

#define HUB_CAPTURE_TICKS	(2 * HZ)

#define HUB_ERR_THRESH		500
#define USEC_PER_SEC		1000000
#define NSEC_PER_SEC		USEC_PER_SEC*1000

volatile int hub_print_usecs = 600 * USEC_PER_SEC;

/* Return success if the hub's crosstalk link is working */
int
hub_xtalk_link_up(nasid_t nasid)
{
	hubreg_t	llp_csr_reg;

	/* Read the IO LLP control status register */
	llp_csr_reg = REMOTE_HUB_L(nasid, IIO_LLP_CSR);

	/* Check if the xtalk link is working */
	if (llp_csr_reg & IIO_LLP_CSR_IS_UP) 
		return(1);

	return(0);

	
}

static char *error_flag_to_type(unsigned char error_flag)
{
    switch(error_flag) {
    case 0x1: return ("NI retries");
    case 0x2: return ("NI SN errors");
    case 0x4: return ("NI CB errors");
    case 0x8: return ("II CB errors");
    case 0x10: return ("II SN errors");
    default: return ("Errors");
    }
}

int
print_hub_error(hubstat_t *hsp, hubreg_t reg,
		int64_t delta, unsigned char error_flag)
{
	int64_t rate;

	reg *= hsp->hs_per_minute;	/* Convert to minutes */
	rate = reg / delta;

	if (rate > HUB_ERR_THRESH) {
		
		if(hsp->hs_maint & error_flag) 
		{
		printk( "Excessive %s (%ld/min) on %s",
			error_flag_to_type(error_flag), rate, hsp->hs_name); 
		}
		else 
		{
		   hsp->hs_maint |= error_flag;
		printk( "Excessive %s (%ld/min) on %s",
			error_flag_to_type(error_flag), rate, hsp->hs_name); 
		}
		return 1;
	} else {
		return 0;
	}
}


int
check_hub_error_rates(hubstat_t *hsp)
{
	int64_t delta = hsp->hs_timestamp - hsp->hs_timebase;
	int printed = 0;

	printed += print_hub_error(hsp, hsp->hs_ni_retry_errors,
				   delta, 0x1);

#if 0
	printed += print_hub_error(hsp, hsp->hs_ni_sn_errors,
				   delta, 0x2);
#endif

	printed += print_hub_error(hsp, hsp->hs_ni_cb_errors,
				   delta, 0x4);


	/* If the hub's xtalk link is not working there is 
	 * no need to print the "Excessive..." warning 
	 * messages
	 */
	if (!hub_xtalk_link_up(hsp->hs_nasid))
		return(printed);


	printed += print_hub_error(hsp, hsp->hs_ii_cb_errors,
				   delta, 0x8);

	printed += print_hub_error(hsp, hsp->hs_ii_sn_errors,
				   delta, 0x10);

	return printed;
}


void
capture_hub_stats(cnodeid_t cnodeid, struct nodepda_s *npda)
{
	nasid_t nasid;
	hubstat_t *hsp = &(npda->hubstats);
	hubreg_t port_error;
	ii_illr_u_t illr;
	int count;
	int overflow = 0;

	/*
	 * If our link wasn't up at boot time, don't worry about error rates.
	 */
	if (!(hsp->hs_ni_port_status & NPS_LINKUP_MASK)) {
		printk("capture_hub_stats: cnode=%d hs_ni_port_status=0x%016lx : link is not up\n",
		cnodeid, hsp->hs_ni_port_status);
		return;
	}

	nasid = COMPACT_TO_NASID_NODEID(cnodeid);

	hsp->hs_timestamp = GET_RTC_COUNTER();

	port_error = REMOTE_HUB_L(nasid, NI_PORT_ERROR_CLEAR);
	count = ((port_error & NPE_RETRYCOUNT_MASK) >> NPE_RETRYCOUNT_SHFT);
	hsp->hs_ni_retry_errors += count;
	if (count == NPE_COUNT_MAX)
		overflow = 1;
	count = ((port_error & NPE_SNERRCOUNT_MASK) >> NPE_SNERRCOUNT_SHFT);
	hsp->hs_ni_sn_errors += count;
	if (count == NPE_COUNT_MAX)
		overflow = 1;
	count = ((port_error & NPE_CBERRCOUNT_MASK) >> NPE_CBERRCOUNT_SHFT);
	hsp->hs_ni_cb_errors += count;
	if (overflow || count == NPE_COUNT_MAX)
		hsp->hs_ni_overflows++;

	if (port_error & NPE_FATAL_ERRORS) {
#ifdef ajm
		hubni_error_handler("capture_hub_stats", 1);
#else
		printk("Error: hubni_error_handler in capture_hub_stats");
#endif
	}

	illr.ii_illr_regval = REMOTE_HUB_L(nasid, IIO_LLP_LOG);
	REMOTE_HUB_S(nasid, IIO_LLP_LOG, 0);

	hsp->hs_ii_sn_errors += illr.ii_illr_fld_s.i_sn_cnt;
	hsp->hs_ii_cb_errors += illr.ii_illr_fld_s.i_cb_cnt;
	if ((illr.ii_illr_fld_s.i_sn_cnt == IIO_LLP_SN_MAX) ||
	    (illr.ii_illr_fld_s.i_cb_cnt == IIO_LLP_CB_MAX))
		hsp->hs_ii_overflows++;

	if (hsp->hs_print) {
		if (check_hub_error_rates(hsp)) {
			hsp->hs_last_print = GET_RTC_COUNTER();
			hsp->hs_print = 0;
		}
	} else {
		if ((GET_RTC_COUNTER() -
		    hsp->hs_last_print) > hub_print_usecs)
			hsp->hs_print = 1;
	}
		
	npda->hubticks = HUB_CAPTURE_TICKS;
}


void
init_hub_stats(cnodeid_t cnodeid, struct nodepda_s *npda)
{
	hubstat_t *hsp = &(npda->hubstats);
	nasid_t nasid = cnodeid_to_nasid(cnodeid);
	bzero(&(npda->hubstats), sizeof(hubstat_t));

	hsp->hs_version = HUBSTAT_VERSION;
	hsp->hs_cnode = cnodeid;
	hsp->hs_nasid = nasid;
	hsp->hs_timebase = GET_RTC_COUNTER();
	hsp->hs_ni_port_status = REMOTE_HUB_L(nasid, NI_PORT_STATUS);

	/* Clear the II error counts. */
	REMOTE_HUB_S(nasid, IIO_LLP_LOG, 0);

	/* Clear the NI counts. */
	REMOTE_HUB_L(nasid, NI_PORT_ERROR_CLEAR);

	hsp->hs_per_minute = (long long)RTC_CYCLES_PER_SEC * 60LL;

	npda->hubticks = HUB_CAPTURE_TICKS;

	/* XX should use kmem_alloc_node */
	hsp->hs_name = (char *)kmalloc(MAX_HUB_PATH, GFP_KERNEL);
	ASSERT_ALWAYS(hsp->hs_name);

	sprintf(hsp->hs_name, "/dev/hw/" EDGE_LBL_MODULE "/%03d/"
	        EDGE_LBL_NODE "/" EDGE_LBL_HUB,
		npda->module_id);

	hsp->hs_last_print = 0;
	hsp->hs_print = 1;

	hub_print_usecs = hub_print_usecs;

#if 0
	printk("init_hub_stats: cnode=%d nasid=%d hs_version=%d hs_ni_port_status=0x%016lx\n",
		cnodeid, nasid, hsp->hs_version, hsp->hs_ni_port_status);
#endif
}

static int
hubstats_ioctl(struct inode *inode, struct file *file,
        unsigned int cmd, unsigned long arg)
{
        cnodeid_t       cnode;
        nodepda_t       *npdap;
        uint64_t        longarg;
        devfs_handle_t  d;

        if ((d = devfs_get_handle_from_inode(inode)) == NULL)
                return -ENODEV;
        cnode = (cnodeid_t)hwgraph_fastinfo_get(d);
        npdap = NODEPDA(cnode);

	if (npdap->hubstats.hs_version != HUBSTAT_VERSION) {
		init_hub_stats(cnode, npdap);
	}

        switch (cmd) {
	case SNDRV_GET_INFOSIZE:
		longarg = sizeof(hubstat_t);
		if (copy_to_user((void *)arg, &longarg, sizeof(longarg))) {
		    return -EFAULT;
		}
		break;

	case SNDRV_GET_HUBINFO:
		/* refresh npda->hubstats */
		capture_hub_stats(cnode, npdap);
		if (copy_to_user((void *)arg, &npdap->hubstats, sizeof(hubstat_t))) {
		    return -EFAULT;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

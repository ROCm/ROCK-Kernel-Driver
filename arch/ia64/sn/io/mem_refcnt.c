/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/types.h>
#include <linux/config.h>
#include <asm/sn/sgi.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/hubspc.h>
#include <asm/sn/iograph.h>
#include <asm/sn/mem_refcnt.h>
#include <asm/sn/hwcntrs.h>
// From numa_hw.h

#define MIGR_COUNTER_MAX_GET(nodeid) \
        (NODEPDA_MCD((nodeid))->migr_system_kparms.migr_threshold_reference)
/*
 * Get the Absolute Theshold
 */
#define MIGR_THRESHOLD_ABS_GET(nodeid) ( \
        MD_MIG_VALUE_THRESH_GET(COMPACT_TO_NASID_NODEID(nodeid)))
/*
 * Get the current Differential Threshold
 */
#define MIGR_THRESHOLD_DIFF_GET(nodeid) \
        (NODEPDA_MCD(nodeid)->migr_as_kparms.migr_base_threshold)

#define NUM_OF_HW_PAGES_PER_SW_PAGE()   (NBPP / MD_PAGE_SIZE)

// #include "migr_control.h"

int
mem_refcnt_attach(devfs_handle_t hub)
{
        devfs_handle_t refcnt_dev;
        
        hwgraph_char_device_add(hub,
                                "refcnt",
                                "hubspc_", 
				&refcnt_dev);
        device_info_set(refcnt_dev, (void*)(ulong)HUBSPC_REFCOUNTERS);

        return (0);
}


/*ARGSUSED*/
int
mem_refcnt_open(devfs_handle_t *devp, mode_t oflag, int otyp, cred_t *crp)
{
        cnodeid_t node;
#ifndef CONFIG_IA64_SGI_SN1
	extern int numnodes;
#endif
        
        ASSERT( (hubspc_subdevice_t)(ulong)device_info_get(*devp) == HUBSPC_REFCOUNTERS );

        if (!cap_able(CAP_MEMORY_MGT)) {
                return (EPERM);
        }

        node = master_node_get(*devp);

        ASSERT( (node >= 0) && (node < numnodes) );

        if (NODEPDA(node)->migr_refcnt_counterbuffer == NULL) {
                return (ENODEV);
        }

        ASSERT( NODEPDA(node)->migr_refcnt_counterbase != NULL );
        ASSERT( NODEPDA(node)->migr_refcnt_cbsize != (size_t)0 );

        return (0);
}

/*ARGSUSED*/
int
mem_refcnt_close(devfs_handle_t dev, int oflag, int otyp, cred_t *crp)
{
        return 0;
}

/*ARGSUSED*/
int
mem_refcnt_mmap(devfs_handle_t dev, vhandl_t *vt, off_t off, size_t len, uint prot)
{
        cnodeid_t node;
        int errcode;
        char* buffer;
        size_t blen;
#ifndef CONFIG_IA64_SGI_SN1
	extern int numnodes;
#endif
        
        ASSERT( (hubspc_subdevice_t)(ulong)device_info_get(dev) == HUBSPC_REFCOUNTERS );

        node = master_node_get(dev);

        ASSERT( (node >= 0) && (node < numnodes) );

        ASSERT( NODEPDA(node)->migr_refcnt_counterbuffer != NULL);
        ASSERT( NODEPDA(node)->migr_refcnt_counterbase != NULL );
        ASSERT( NODEPDA(node)->migr_refcnt_cbsize != 0 );

        /*
         * XXXX deal with prot's somewhere around here....
         */

        buffer = NODEPDA(node)->migr_refcnt_counterbuffer;
        blen = NODEPDA(node)->migr_refcnt_cbsize;

        /*
         * Force offset to be a multiple of sizeof(refcnt_t)
         * We round up.
         */

        off = (((off - 1)/sizeof(refcnt_t)) + 1) * sizeof(refcnt_t);

        if ( ((buffer + blen) - (buffer + off + len)) < 0 ) {
                return (EPERM);
        }

        errcode = v_mapphys(vt,
                            buffer + off,
                            len);

        return errcode;
}

/*ARGSUSED*/
int
mem_refcnt_unmap(devfs_handle_t dev, vhandl_t *vt)
{
        return 0;
}

/* ARGSUSED */
int
mem_refcnt_ioctl(devfs_handle_t dev,
                 int cmd,
                 void *arg,
                 int mode,
                 cred_t *cred_p,
                 int *rvalp)
{
        cnodeid_t node;
        int errcode;
	extern int numnodes;
        
        ASSERT( (hubspc_subdevice_t)(ulong)device_info_get(dev) == HUBSPC_REFCOUNTERS );

        node = master_node_get(dev);

        ASSERT( (node >= 0) && (node < numnodes) );

        ASSERT( NODEPDA(node)->migr_refcnt_counterbuffer != NULL);
        ASSERT( NODEPDA(node)->migr_refcnt_counterbase != NULL );
        ASSERT( NODEPDA(node)->migr_refcnt_cbsize != 0 );

        errcode = 0;
        
        switch (cmd) {
        case RCB_INFO_GET:
        {
                rcb_info_t rcb;
                
                rcb.rcb_len = NODEPDA(node)->migr_refcnt_cbsize;
                
                rcb.rcb_sw_sets = NODEPDA(node)->migr_refcnt_numsets;
                rcb.rcb_sw_counters_per_set = numnodes;
                rcb.rcb_sw_counter_size = sizeof(refcnt_t);

                rcb.rcb_base_pages = NODEPDA(node)->migr_refcnt_numsets /
                                     NUM_OF_HW_PAGES_PER_SW_PAGE();  
                rcb.rcb_base_page_size = NBPP;
                rcb.rcb_base_paddr = ctob(slot_getbasepfn(node, 0));
                
                rcb.rcb_cnodeid = node;
                rcb.rcb_granularity = MD_PAGE_SIZE;
#ifdef notyet
                rcb.rcb_hw_counter_max = MIGR_COUNTER_MAX_GET(node);
                rcb.rcb_diff_threshold = MIGR_THRESHOLD_DIFF_GET(node);
#endif
                rcb.rcb_abs_threshold = MIGR_THRESHOLD_ABS_GET(node);
                rcb.rcb_num_slots = node_getnumslots(node);

                if (COPYOUT(&rcb, arg, sizeof(rcb_info_t))) {
                        errcode = EFAULT;
                }

                break;
        }
        case RCB_SLOT_GET:
        {
                rcb_slot_t slot[MAX_MEM_SLOTS];
                int s;
                int nslots;

                nslots = node_getnumslots(node);
                ASSERT(nslots <= MAX_MEM_SLOTS);
                for (s = 0; s < nslots; s++) {
                        slot[s].base = (uint64_t)ctob(slot_getbasepfn(node, s));
#ifdef notyet
                        slot[s].size  = (uint64_t)ctob(slot_getsize(node, s));
#else
                        slot[s].size  = (uint64_t)1;
#endif
                }
                if (COPYOUT(&slot[0], arg, nslots * sizeof(rcb_slot_t))) {
                        errcode = EFAULT;
                }
                
                *rvalp = nslots;
                break;
        }
                
        default:
                errcode = EINVAL;
                break;

        }
        
        return errcode;
}

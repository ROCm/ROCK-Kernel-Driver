/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997, 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/hw_irq.h>
#include <asm/system.h>
#include <asm/sn/sgi.h>
#include <asm/uaccess.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/xtalk/xtalk.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/intr.h>
#include <asm/sn/sn2/shub_mmr.h>
#include <asm/sn/sn2/shub_mmr_t.h>
#include <asm/sal.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sndrv.h>
#include <asm/sn/sn2/shubio.h>

#define SHUB_NUM_ECF_REGISTERS 8

static uint32_t	shub_perf_counts[SHUB_NUM_ECF_REGISTERS];

static shubreg_t shub_perf_counts_regs[SHUB_NUM_ECF_REGISTERS] = {
	SH_PERFORMANCE_COUNTER0,
	SH_PERFORMANCE_COUNTER1,
	SH_PERFORMANCE_COUNTER2,
	SH_PERFORMANCE_COUNTER3,
	SH_PERFORMANCE_COUNTER4,
	SH_PERFORMANCE_COUNTER5,
	SH_PERFORMANCE_COUNTER6,
	SH_PERFORMANCE_COUNTER7
};

static inline void
shub_mmr_write(cnodeid_t cnode, shubreg_t reg, uint64_t val)
{
	int		   nasid = cnodeid_to_nasid(cnode);
	volatile uint64_t *addr = (uint64_t *)(GLOBAL_MMR_ADDR(nasid, reg));

	*addr = val;
	__ia64_mf_a();
}

static inline void
shub_mmr_write_iospace(cnodeid_t cnode, shubreg_t reg, uint64_t val)
{
	int		   nasid = cnodeid_to_nasid(cnode);

	REMOTE_HUB_S(nasid, reg, val);
}

static inline void
shub_mmr_write32(cnodeid_t cnode, shubreg_t reg, uint32_t val)
{
	int		   nasid = cnodeid_to_nasid(cnode);
	volatile uint32_t *addr = (uint32_t *)(GLOBAL_MMR_ADDR(nasid, reg));

	*addr = val;
	__ia64_mf_a();
}

static inline uint64_t
shub_mmr_read(cnodeid_t cnode, shubreg_t reg)
{
	int		  nasid = cnodeid_to_nasid(cnode);
	volatile uint64_t val;

	val = *(uint64_t *)(GLOBAL_MMR_ADDR(nasid, reg));
	__ia64_mf_a();

	return val;
}

static inline uint64_t
shub_mmr_read_iospace(cnodeid_t cnode, shubreg_t reg)
{
	int		  nasid = cnodeid_to_nasid(cnode);

	return REMOTE_HUB_L(nasid, reg);
}

static inline uint32_t
shub_mmr_read32(cnodeid_t cnode, shubreg_t reg)
{
	int		  nasid = cnodeid_to_nasid(cnode);
	volatile uint32_t val;

	val = *(uint32_t *)(GLOBAL_MMR_ADDR(nasid, reg));
	__ia64_mf_a();

	return val;
}

static int
reset_shub_stats(cnodeid_t cnode)
{
	int i;

	for (i=0; i < SHUB_NUM_ECF_REGISTERS; i++) {
		shub_perf_counts[i] = 0;
		shub_mmr_write32(cnode, shub_perf_counts_regs[i], 0);
	}
	return 0;
}

static int
configure_shub_stats(cnodeid_t cnode, unsigned long arg)
{
	uint64_t	*p = (uint64_t *)arg;
	uint64_t	i;
	uint64_t	regcnt;
	uint64_t	regval[2];

	if (copy_from_user((void *)&regcnt, p, sizeof(regcnt)))
	    return -EFAULT;

	for (p++, i=0; i < regcnt; i++, p += 2) {
		if (copy_from_user((void *)regval, (void *)p, sizeof(regval)))
		    return -EFAULT;
		if (regval[0] & 0x7) {
		    printk("Error: configure_shub_stats: unaligned address 0x%016lx\n", regval[0]);
		    return -EINVAL;
		}
		shub_mmr_write(cnode, (shubreg_t)regval[0], regval[1]);
	}
	return 0;
}

static int
capture_shub_stats(cnodeid_t cnode, uint32_t *counts)
{
	int 		i;

	for (i=0; i < SHUB_NUM_ECF_REGISTERS; i++) {
		counts[i] = shub_mmr_read32(cnode, shub_perf_counts_regs[i]);
	}
	return 0;
}

static int
shubstats_ioctl(struct inode *inode, struct file *file,
        unsigned int cmd, unsigned long arg)
{
	cnodeid_t       cnode;
	uint64_t        longarg;
	uint64_t        intarg;
	uint64_t        regval[2];
	int		nasid;

        cnode = (cnodeid_t)(u64)file->f_dentry->d_fsdata;
        if (cnode < 0 || cnode >= numnodes)
                return -ENODEV;

        switch (cmd) {
	case SNDRV_SHUB_CONFIGURE:
		return configure_shub_stats(cnode, arg);
		break;

	case SNDRV_SHUB_RESETSTATS:
		reset_shub_stats(cnode);
		break;

	case SNDRV_SHUB_INFOSIZE:
		longarg = sizeof(shub_perf_counts);
		if (copy_to_user((void *)arg, &longarg, sizeof(longarg))) {
		    return -EFAULT;
		}
		break;

	case SNDRV_SHUB_GETSTATS:
		capture_shub_stats(cnode, shub_perf_counts);
		if (copy_to_user((void *)arg, shub_perf_counts,
				       	sizeof(shub_perf_counts))) {
		    return -EFAULT;
		}
		break;

	case SNDRV_SHUB_GETNASID:
		nasid = cnodeid_to_nasid(cnode);
		if (copy_to_user((void *)arg, &nasid,
				       	sizeof(nasid))) {
		    return -EFAULT;
		}
		break;

	case SNDRV_SHUB_GETMMR32:
		intarg = shub_mmr_read32(cnode, arg);
		if (copy_to_user((void *)arg, &intarg,
					sizeof(intarg))) {
		    return -EFAULT;
		}
		break;
 
	case SNDRV_SHUB_GETMMR64:
	case SNDRV_SHUB_GETMMR64_IO:
		if (cmd == SNDRV_SHUB_GETMMR64)
		    longarg = shub_mmr_read(cnode, arg);
		else
		    longarg = shub_mmr_read_iospace(cnode, arg);
		if (copy_to_user((void *)arg, &longarg, sizeof(longarg)))
		    return -EFAULT;
		break;
 
	case SNDRV_SHUB_PUTMMR64:
	case SNDRV_SHUB_PUTMMR64_IO:
		if (copy_from_user((void *)regval, (void *)arg, sizeof(regval)))
		    return -EFAULT;
		if (regval[0] & 0x7) {
		    printk("Error: configure_shub_stats: unaligned address 0x%016lx\n", regval[0]);
		    return -EINVAL;
		}
		if (cmd == SNDRV_SHUB_PUTMMR64)
		    shub_mmr_write(cnode, (shubreg_t)regval[0], regval[1]);
		else
		    shub_mmr_write_iospace(cnode, (shubreg_t)regval[0], regval[1]);
		break;
 
	default:
		return -EINVAL;
	}

	return 0;
}

struct file_operations shub_mon_fops = {
	        .ioctl          = shubstats_ioctl,
};

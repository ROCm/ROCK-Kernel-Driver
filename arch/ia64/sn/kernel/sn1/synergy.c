/*
 * SN1 Platform specific synergy Support
 *
 * Copyright (C) 2000-2002 Silicon Graphics, Inc. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License 
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 * Further, this software is distributed without any warranty that it is 
 * free of the rightful claim of any third person regarding infringement 
 * or the like.  Any license provided herein, whether implied or 
 * otherwise, applies only to this software file.  Patent licenses, if 
 * any, provided herein do not apply to combinations of this program with 
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy, 
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>

#include <asm/ptrace.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/smp.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/sn1/bedrock.h>
#include <asm/sn/intr.h>
#include <asm/sn/addrs.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/sn1/synergy.h>
#include <asm/sn/sndrv.h>

int bit_pos_to_irq(int bit);
void setclear_mask_b(int irq, int cpuid, int set);
void setclear_mask_a(int irq, int cpuid, int set);
void * kmalloc(size_t size, int flags);

static int synergy_perf_initialized = 0;

void
synergy_intr_alloc(int bit, int cpuid) {
	return;
}

int
synergy_intr_connect(int bit, 
		int cpuid)
{
	int irq;
	unsigned is_b;

	irq = bit_pos_to_irq(bit);

	is_b = (cpuid_to_slice(cpuid)) & 1;
	if (is_b) {
		setclear_mask_b(irq,cpuid,1);
		setclear_mask_a(irq,cpuid, 0);
	} else {
		setclear_mask_a(irq, cpuid, 1);
		setclear_mask_b(irq, cpuid, 0);
	}
	return 0;
}
void
setclear_mask_a(int irq, int cpuid, int set)
{
	int synergy;
	int nasid;
	int reg_num;
	unsigned long mask;
	unsigned long addr;
	unsigned long reg;
	unsigned long val;
	int my_cnode, my_synergy;
	int target_cnode, target_synergy;

        /*
         * Perform some idiot checks ..
         */
        if ( (irq < 0) || (irq > 255) ||
                (cpuid < 0) || (cpuid > 512) ) {
                printk("clear_mask_a: Invalid parameter irq %d cpuid %d\n", irq, cpuid);
		return;
	}

	target_cnode = cpuid_to_cnodeid(cpuid);
	target_synergy = cpuid_to_synergy(cpuid);
	my_cnode = cpuid_to_cnodeid(smp_processor_id());
	my_synergy = cpuid_to_synergy(smp_processor_id());

	reg_num = irq / 64;
	mask = 1;
	mask <<= (irq % 64);
	switch (reg_num) {
		case 0: 
			reg = VEC_MASK0A;
			addr = VEC_MASK0A_ADDR;
			break;
		case 1: 
			reg = VEC_MASK1A;
			addr = VEC_MASK1A_ADDR;
			break;
		case 2: 
			reg = VEC_MASK2A;
			addr = VEC_MASK2A_ADDR;
			break;
		case 3: 
			reg = VEC_MASK3A;
			addr = VEC_MASK3A_ADDR;
			break;
		default:
			reg = addr = 0;
			break;
	}
	if (my_cnode == target_cnode && my_synergy == target_synergy) {
		// local synergy
		val = READ_LOCAL_SYNERGY_REG(addr);
		if (set) {
			val |= mask;
		} else {
			val &= ~mask;
		}
		WRITE_LOCAL_SYNERGY_REG(addr, val);
		val = READ_LOCAL_SYNERGY_REG(addr);
	} else { /* remote synergy */
		synergy = cpuid_to_synergy(cpuid);
		nasid = cpuid_to_nasid(cpuid);
		val = REMOTE_SYNERGY_LOAD(nasid, synergy, reg);
		if (set) {
			val |= mask;
		} else {
			val &= ~mask;
		}
		REMOTE_SYNERGY_STORE(nasid, synergy, reg, val);
	}
}

void
setclear_mask_b(int irq, int cpuid, int set)
{
	int synergy;
	int nasid;
	int reg_num;
	unsigned long mask;
	unsigned long addr;
	unsigned long reg;
	unsigned long val;
	int my_cnode, my_synergy;
	int target_cnode, target_synergy;

	/*
	 * Perform some idiot checks ..
	 */
	if ( (irq < 0) || (irq > 255) ||
		(cpuid < 0) || (cpuid > 512) ) {
		printk("clear_mask_b: Invalid parameter irq %d cpuid %d\n", irq, cpuid);
		return;
	}

	target_cnode = cpuid_to_cnodeid(cpuid);
	target_synergy = cpuid_to_synergy(cpuid);
	my_cnode = cpuid_to_cnodeid(smp_processor_id());
	my_synergy = cpuid_to_synergy(smp_processor_id());

	reg_num = irq / 64;
	mask = 1;
	mask <<= (irq % 64);
	switch (reg_num) {
		case 0: 
			reg = VEC_MASK0B;
			addr = VEC_MASK0B_ADDR;
			break;
		case 1: 
			reg = VEC_MASK1B;
			addr = VEC_MASK1B_ADDR;
			break;
		case 2: 
			reg = VEC_MASK2B;
			addr = VEC_MASK2B_ADDR;
			break;
		case 3: 
			reg = VEC_MASK3B;
			addr = VEC_MASK3B_ADDR;
			break;
		default:
			reg = addr = 0;
			break;
	}
	if (my_cnode == target_cnode && my_synergy == target_synergy) {
		// local synergy
		val = READ_LOCAL_SYNERGY_REG(addr);
		if (set) {
			val |= mask;
		} else {
			val &= ~mask;
		}
		WRITE_LOCAL_SYNERGY_REG(addr, val);
		val = READ_LOCAL_SYNERGY_REG(addr);
	} else { /* remote synergy */
		synergy = cpuid_to_synergy(cpuid);
		nasid = cpuid_to_nasid(cpuid);
		val = REMOTE_SYNERGY_LOAD(nasid, synergy, reg);
		if (set) {
			val |= mask;
		} else {
			val &= ~mask;
		}
		REMOTE_SYNERGY_STORE(nasid, synergy, reg, val);
	}
}

/*
 * Synergy perf stats. Multiplexed via timer_interrupt.
 */

static int
synergy_perf_append(uint64_t modesel)
{
	int		cnode;
	nodepda_t       *npdap;
	synergy_perf_t	*p;
	int		checked = 0;
	int		err = 0;

	/* bit 45 is enable */
	modesel |= (1UL << 45);

	for (cnode=0; cnode < numnodes; cnode++) {
		/* for each node, insert a new synergy_perf entry */
		if ((npdap = NODEPDA(cnode)) == NULL) {
			printk("synergy_perf_append: cnode=%d NODEPDA(cnode)==NULL, nodepda=%p\n", cnode, (void *)nodepda);
			continue;
		}

		if (npdap->synergy_perf_enabled) {
			/* user must disable counting to append new events */
			err = -EBUSY;
			break;
		}

		if (!checked && npdap->synergy_perf_data != NULL) {
			checked = 1;
			for (p = npdap->synergy_perf_first; ;) {
				if (p->modesel == modesel)
					return 0; /* event already registered */
				if ((p = p->next) == npdap->synergy_perf_first)
					break;
			}
		}

		/* XX use kmem_alloc_node() when it is implemented */
		p = (synergy_perf_t *)kmalloc(sizeof(synergy_perf_t), GFP_KERNEL);
		if ((((uint64_t)p) & 7UL) != 0)
			BUG(); /* bad alignment */
		if (p == NULL) {
			err = -ENOMEM;
			break;
		}
		else {
			memset(p, 0, sizeof(synergy_perf_t));
			p->modesel = modesel;

			spin_lock_irq(&npdap->synergy_perf_lock);
			if (npdap->synergy_perf_data == NULL) {
				/* circular list */
				p->next = p;
				npdap->synergy_perf_first = p;
				npdap->synergy_perf_data = p;
			}
			else {
				p->next = npdap->synergy_perf_data->next;
				npdap->synergy_perf_data->next = p;
			}
			spin_unlock_irq(&npdap->synergy_perf_lock);
		}
	}

	return err;
}

static void
synergy_perf_set_freq(int freq)
{
	int		cnode;
	nodepda_t	*npdap;

	for (cnode=0; cnode < numnodes; cnode++) {
		if ((npdap = NODEPDA(cnode)) != NULL)
			npdap->synergy_perf_freq = freq;
	}
}

static void
synergy_perf_set_enable(int enable)
{
	int		cnode;
	nodepda_t	*npdap;

	for (cnode=0; cnode < numnodes; cnode++) {
		if ((npdap = NODEPDA(cnode)) != NULL)
			npdap->synergy_perf_enabled = enable;
	}
	printk("NOTICE: synergy perf counting %sabled on all nodes\n", enable ? "en" : "dis");
}

static int
synergy_perf_size(nodepda_t *npdap)
{
	synergy_perf_t	*p;
	int		n;

	if (npdap->synergy_perf_enabled == 0) {
		/* no stats to return */
		return 0;
	}

	spin_lock_irq(&npdap->synergy_perf_lock);
	for (n=0, p = npdap->synergy_perf_first; p;) {
		n++;
		p = p->next;
		if (p == npdap->synergy_perf_first)
			break;
	}
	spin_unlock_irq(&npdap->synergy_perf_lock);

	/* bytes == n pairs of {event,counter} */
	return n * 2 * sizeof(uint64_t);
}

static int
synergy_perf_ioctl(struct inode *inode, struct file *file,
        unsigned int cmd, unsigned long arg)
{
	int             cnode;
	nodepda_t       *npdap;
	synergy_perf_t	*p;
	int		intarg;
	int		fsb;
	uint64_t	longarg;
	uint64_t	*stats;
	int		n;
	devfs_handle_t	d;
	arbitrary_info_t info;
	
	if ((d = devfs_get_handle_from_inode(inode)) == NULL)
		return -ENODEV;
	info = hwgraph_fastinfo_get(d);

	cnode = SYNERGY_PERF_INFO_CNODE(info);
	fsb = SYNERGY_PERF_INFO_FSB(info);
	npdap = NODEPDA(cnode);

	switch (cmd) {
	case SNDRV_GET_SYNERGY_VERSION:
		/* return int, version of data structure for SNDRV_GET_SYNERGYINFO */
		intarg = 1; /* version 1 */
		if (copy_to_user((void *)arg, &intarg, sizeof(intarg)))
		    return -EFAULT;
		break;

	case SNDRV_GET_INFOSIZE:
		/* return int, sizeof buf needed for SYNERGY_PERF_GET_STATS */
		intarg = synergy_perf_size(npdap);
		if (copy_to_user((void *)arg, &intarg, sizeof(intarg)))
		    return -EFAULT;
		break;

	case SNDRV_GET_SYNERGYINFO:
		/* return array of event/value pairs, this node only */
		if ((intarg = synergy_perf_size(npdap)) <= 0)
			return -ENODATA;
		if ((stats = (uint64_t *)kmalloc(intarg, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		spin_lock_irq(&npdap->synergy_perf_lock);
		for (n=0, p = npdap->synergy_perf_first; p;) {
			stats[n++] = p->modesel;
			if (p->intervals > 0)
			    stats[n++] = p->counts[fsb] * p->total_intervals / p->intervals;
			else
			    stats[n++] = 0;
			p = p->next;
			if (p == npdap->synergy_perf_first)
				break;
		}
		spin_unlock_irq(&npdap->synergy_perf_lock);

		if (copy_to_user((void *)arg, stats, intarg)) {
		    kfree(stats);
		    return -EFAULT;
		}

		kfree(stats);
		break;

	case SNDRV_SYNERGY_APPEND:
		/* reads 64bit event, append synergy perf event to all nodes  */
		if (copy_from_user(&longarg, (void *)arg, sizeof(longarg)))
		    return -EFAULT;
		return synergy_perf_append(longarg);
		break;

	case SNDRV_GET_SYNERGY_STATUS:
		/* return int, 1 if enabled else 0 */
		intarg = npdap->synergy_perf_enabled;
		if (copy_to_user((void *)arg, &intarg, sizeof(intarg)))
		    return -EFAULT;
		break;

	case SNDRV_SYNERGY_ENABLE:
		/* read int, if true enable counting else disable */
		if (copy_from_user(&intarg, (void *)arg, sizeof(intarg)))
		    return -EFAULT;
		synergy_perf_set_enable(intarg);
		break;

	case SNDRV_SYNERGY_FREQ:
		/* read int, set jiffies per update */ 
		if (copy_from_user(&intarg, (void *)arg, sizeof(intarg)))
		    return -EFAULT;
		if (intarg < 0 || intarg >= HZ)
			return -EINVAL;
		synergy_perf_set_freq(intarg);
		break;

	default:
		printk("Warning: invalid ioctl %d on synergy mon for cnode=%d fsb=%d\n", cmd, cnode, fsb);
		return -EINVAL;
	}
	return(0);
}

struct file_operations synergy_mon_fops = {
        ioctl:		synergy_perf_ioctl,
};

void
synergy_perf_update(int cpu)
{
	nasid_t		nasid;
	cnodeid_t       cnode;
	struct nodepda_s *npdap;

	/*
	 * synergy_perf_initialized is set by synergy_perf_init()
	 * which is called last thing by sn_mp_setup(), i.e. well
	 * after nodepda has been initialized.
	 */
	if (!synergy_perf_initialized)
		return;

	cnode = cpuid_to_cnodeid(cpu);
	npdap = NODEPDA(cnode);

	if (npdap == NULL || cnode < 0 || cnode >= numnodes)
		/* this should not happen: still in early io init */
		return;

#if 0
	/* use this to check nodepda initialization */
	if (((uint64_t)npdap) & 0x7) {
		printk("\nERROR on cpu %d : cnode=%d, npdap == %p, not aligned\n", cpu, cnode, npdap);
		BUG();
	}
#endif

	if (npdap->synergy_perf_enabled == 0 || npdap->synergy_perf_data == NULL) {
		/* Not enabled, or no events to monitor */
		return;
	}

	if (npdap->synergy_inactive_intervals++ % npdap->synergy_perf_freq != 0) {
		/* don't multiplex on every timer interrupt */
		return;
	}

	/*
	 * Read registers for last interval and increment counters.
	 * Hold the per-node synergy_perf_lock so concurrent readers get
	 * consistent values.
	 */
	spin_lock_irq(&npdap->synergy_perf_lock);

	nasid = cpuid_to_nasid(cpu);
	npdap->synergy_active_intervals++;
	npdap->synergy_perf_data->intervals++;
	npdap->synergy_perf_data->total_intervals = npdap->synergy_active_intervals;

	npdap->synergy_perf_data->counts[0] += 0xffffffffffUL &
		REMOTE_SYNERGY_LOAD(nasid, 0, PERF_CNTR0_A);

	npdap->synergy_perf_data->counts[1] += 0xffffffffffUL &
		REMOTE_SYNERGY_LOAD(nasid, 1, PERF_CNTR0_B);

	/* skip to next in circular list */
	npdap->synergy_perf_data = npdap->synergy_perf_data->next;

	spin_unlock_irq(&npdap->synergy_perf_lock);

	/* set the counter 0 selection modes for both A and B */
	REMOTE_SYNERGY_STORE(nasid, 0, PERF_CNTL0_A, npdap->synergy_perf_data->modesel);
	REMOTE_SYNERGY_STORE(nasid, 1, PERF_CNTL0_B, npdap->synergy_perf_data->modesel);

	/* and reset the counter registers to zero */
	REMOTE_SYNERGY_STORE(nasid, 0, PERF_CNTR0_A, 0UL);
	REMOTE_SYNERGY_STORE(nasid, 1, PERF_CNTR0_B, 0UL);
}

void
synergy_perf_init(void)
{
	printk("synergy_perf_init(), counting is initially disabled\n");
	synergy_perf_initialized++;
}

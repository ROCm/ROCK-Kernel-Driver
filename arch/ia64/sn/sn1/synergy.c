
/*
 * SN1 Platform specific synergy Support
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 Alan Mayer (ajm@sgi.com)
 */



#include <linux/config.h>
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
#include <asm/sn/synergy.h>

int bit_pos_to_irq(int bit);
void setclear_mask_b(int irq, int cpuid, int set);
void setclear_mask_a(int irq, int cpuid, int set);
void * kmalloc(size_t size, int flags);


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

#if defined(CONFIG_IA64_SGI_SYNERGY_PERF)

/*
 * Synergy perf registers. Multiplexed via timer_interrupt
 */
static struct proc_dir_entry *synergy_perf_proc = NULL;

/*
 * read handler for /proc/synergy
 */
static int
synergy_perf_read_proc (char *page, char **start, off_t off,
                                 int count, int *eof, void *data)
{
	cnodeid_t       cnode;
	nodepda_t       *npdap;
	synergy_perf_t	*p;
	int		len = 0;

	len += sprintf(page+len, "# cnode module slot event synergy-A synergy-B\n");

	/* walk the event list for each node */
	for (cnode=0; cnode < numnodes; cnode++) {
		npdap = NODEPDA(cnode);
		if (npdap->synergy_perf_enabled == 0) {
			len += sprintf(page+len, "# DISABLED\n");
			break;
		}

		spin_lock_irq(&npdap->synergy_perf_lock);
		for (p = npdap->synergy_perf_first; p;) {
			uint64_t cnt_a=0, cnt_b=0;

			if (p->intervals > 0) {
				cnt_a = p->counts[0] * npdap->synergy_active_intervals / p->intervals;
				cnt_b = p->counts[1] * npdap->synergy_active_intervals / p->intervals;
			}

			len += sprintf(page+len, "%d %d %d %12lx %lu %lu\n",
				(int)cnode, (int)npdap->module_id, (int)npdap->slotdesc,
				p->modesel, cnt_a, cnt_b);

			p = p->next;
			if (p == npdap->synergy_perf_first)
				break;
		}
		spin_unlock_irq(&npdap->synergy_perf_lock);
	}

	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return len;
}

static int
synergy_perf_append(uint64_t modesel)
{
	int		cnode;
	nodepda_t       *npdap;
	synergy_perf_t	*p;
	int		err = 0;

	/* bit 45 is enable */
	modesel |= (1UL << 45);

	for (cnode=0; cnode < numnodes; cnode++) {
		/* for each node, insert a new synergy_perf entry */
		if ((npdap = NODEPDA(cnode)) == NULL) {
			printk("synergy_perf_append: cnode=%d NODEPDA(cnode)==NULL, nodepda=%p\n", cnode, nodepda);
			continue;
		}

		/* XX use kmem_alloc_node() when it is implemented */
		p = (synergy_perf_t *)kmalloc(sizeof(synergy_perf_t), GFP_KERNEL);
		if (p == NULL)
			err = -ENOMEM;
		else {
			memset(p, 0, sizeof(synergy_perf_t));
			p->modesel = modesel;
			if (npdap->synergy_perf_data == NULL) {
				/* circular list */
				p->next = p;
				npdap->synergy_perf_data = p;
				npdap->synergy_perf_first = p;
			}
			else {
				/*
				 * Jumble up the insertion order so we get better sampling.
				 * Once the list is complete, "first" stays the same so the
				 * reporting order is consistent.
				 */
				p->next = npdap->synergy_perf_first->next;
				npdap->synergy_perf_first->next = p;
				npdap->synergy_perf_first = p->next;
			}
		}
	}

	return err;
}

static int
synergy_perf_write_proc (struct file *file, const char *buffer,
                                unsigned long count, void *data)
{
	int		cnode;
	nodepda_t       *npdap;
	uint64_t	modesel;
	char		cmd[64];
	extern long	atoi(char *);
    
	if (count == sizeof(uint64_t)) {
	    if (copy_from_user(&modesel, buffer, sizeof(uint64_t)))
		    return -EFAULT;
	    synergy_perf_append(modesel);
	}
	else {
	    if (copy_from_user(cmd, buffer, count < sizeof(cmd) ? count : sizeof(cmd)))
		    return -EFAULT;
	    if (strncmp(cmd, "enable", 6) == 0) {
		/* enable counting */
		for (cnode=0; cnode < numnodes; cnode++) {
			npdap = NODEPDA(cnode);
			npdap->synergy_perf_enabled = 1;
		}
		printk("NOTICE: synergy perf counting enabled\n");
	    }
	    else
	    if (strncmp(cmd, "disable", 7) == 0) {
		/* disable counting */
		for (cnode=0; cnode < numnodes; cnode++) {
			npdap = NODEPDA(cnode);
			npdap->synergy_perf_enabled = 0;
		}
		printk("NOTICE: synergy perf counting disabled\n");
	    }
	    else
	    if (strncmp(cmd, "frequency", 9) == 0) {
		/* set the update frequency (timer-interrupts per update) */
		int freq;

		if (count < 12)
			return -EINVAL;
		freq = atoi(cmd + 10);
		if (freq <= 0 || freq > 100)
			return -EINVAL;
		for (cnode=0; cnode < numnodes; cnode++) {
			npdap = NODEPDA(cnode);
			npdap->synergy_perf_freq = (uint64_t)freq;
		}
		printk("NOTICE: synergy perf freq set to %d\n", freq);
	    }
	    else
		return -EINVAL;
	}
	
	return count;
}

void
synergy_perf_update(int cpu)
{
	nasid_t		nasid;
	cnodeid_t       cnode = cpuid_to_cnodeid(cpu);
	struct nodepda_s *npdap;
	extern struct nodepda_s *nodepda;

	if (nodepda == NULL || (npdap=NODEPDA(cnode)) == NULL || npdap->synergy_perf_enabled == 0 ||
		npdap->synergy_perf_data == NULL) {
		/* I/O not initialized, or not enabled, or no events to monitor */
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
        if ((synergy_perf_proc = create_proc_entry("synergy", 0644, NULL)) != NULL) {
                synergy_perf_proc->read_proc = synergy_perf_read_proc;
                synergy_perf_proc->write_proc = synergy_perf_write_proc;
                printk("markgw: synergy_perf_init()\n");
        }
}

#endif /* CONFIG_IA64_SGI_SYNERGY_PERF */


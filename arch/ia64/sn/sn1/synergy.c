
/*
 * SN1 Platform specific synergy Support
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 Alan Mayer (ajm@sgi.com)
 */



#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

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

extern struct sn1_cnode_action_list *sn1_node_actions[];


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
int nasid;

nasid = cpuid_to_nasid(cpuid);
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

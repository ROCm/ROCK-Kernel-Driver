/*
 * This provides a debug register allocation mechanism, to be 
 * used by all debuggers, which need debug registers.
 *
 * Author: vamsi_krishna@in.ibm.com
 *	   bharata@in.ibm.com
 */
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <asm/system.h>
#include <asm/debugreg.h>

struct debugreg dr_list[DR_MAX];
unsigned long dr7_global_mask = 0;
static spinlock_t dr_lock = SPIN_LOCK_UNLOCKED;

static inline void set_dr7_global_mask(int regnum) 
{
	switch (regnum) {
		case 0: dr7_global_mask |= DR7_DR0_BITS; break;
		case 1: dr7_global_mask |= DR7_DR1_BITS; break;
		case 2: dr7_global_mask |= DR7_DR2_BITS; break;
		case 3: dr7_global_mask |= DR7_DR3_BITS; break;
	}
	return;
}

static inline void clear_dr7_global_mask(int regnum) 
{
	switch (regnum) {
		case 0: dr7_global_mask &= ~DR7_DR0_BITS; break;
		case 1: dr7_global_mask &= ~DR7_DR1_BITS; break;
		case 2: dr7_global_mask &= ~DR7_DR2_BITS; break;
		case 3: dr7_global_mask &= ~DR7_DR3_BITS; break;
	}
	return;
}

static int get_dr(int regnum, int flag)
{
	if ((flag == DR_ALLOC_GLOBAL) && (dr_list[regnum].flag == DR_UNUSED)) {
		dr_list[regnum].flag = DR_GLOBAL;
		set_dr7_global_mask(regnum);
		return regnum;
	}
	else if ((dr_list[regnum].flag == DR_UNUSED) || (dr_list[regnum].flag == DR_LOCAL)) {
		dr_list[regnum].use_count++;
		dr_list[regnum].flag = DR_LOCAL;
		return regnum;
	}
	return -1;
}
	
static int get_any_dr(int flag)
{
	int i;
	if (flag == DR_ALLOC_LOCAL) {
		for (i = 0; i < DR_MAX; i++) {
			if (dr_list[i].flag == DR_LOCAL) {
				dr_list[i].use_count++;
				return i;
			} else if (dr_list[i].flag == DR_UNUSED) {
				dr_list[i].flag = DR_LOCAL;
				dr_list[i].use_count = 1;
				return i;
			}
		}
	} else {
		for (i = DR_MAX-1; i >= 0; i--) {
			if (dr_list[i].flag == DR_UNUSED) {
				dr_list[i].flag = DR_GLOBAL;
				set_dr7_global_mask(i);
				return i;
			}
		}
	}
	return -1;
}

static inline void dr_free_local(int regnum)
{
	if (! (--dr_list[regnum].use_count)) 
		dr_list[regnum].flag = DR_UNUSED;
	return;
}

static inline void dr_free_global(int regnum) 
{
	dr_list[regnum].flag = DR_UNUSED;
	dr_list[regnum].use_count = 0;
	clear_dr7_global_mask(regnum);
	return;
}
		
int dr_alloc(int regnum, int flag)
{
	int ret;
	
	spin_lock(&dr_lock);
	if (regnum == DR_ANY) 
		ret = get_any_dr(flag);
	else if (regnum >= DR_MAX)
		ret = -1;
	else
		ret = get_dr(regnum, flag);
	spin_unlock(&dr_lock);
	return ret;
}

int dr_free(int regnum)
{
	spin_lock(&dr_lock);
	if (regnum >= DR_MAX || dr_list[regnum].flag == DR_UNUSED) {
		spin_unlock(&dr_lock);
		return -1;
	}
	if (dr_list[regnum].flag == DR_LOCAL)
		dr_free_local(regnum);
	else 
		dr_free_global(regnum);
	spin_unlock(&dr_lock);
	return 0;
}

void dr_inc_use_count(unsigned long mask)
{
	int i;
	
	spin_lock(&dr_lock);
	for (i =0; i < DR_MAX; i++) {
		if (DR_IS_LOCAL(mask, i))
			dr_list[i].use_count++;
	}
	spin_unlock(&dr_lock);
}

void dr_dec_use_count(unsigned long mask)
{
	int i;
	
	spin_lock(&dr_lock);
	for (i =0; i < DR_MAX; i++) {
		if (DR_IS_LOCAL(mask, i))
			dr_free_local(i);
	}
	spin_unlock(&dr_lock);
}

/*
 * This routine decides if the ptrace request is for enabling or disabling 
 * a debug reg, and accordingly calls dr_alloc() or dr_free().
 *
 * gdb uses ptrace to write to debug registers. It assumes that writing to
 * debug register always succeds and it doesn't check the return value of 
 * ptrace. Now with this new global debug register allocation/freeing,
 * ptrace request for a local debug register can fail, if the required debug
 * register is already globally allocated. Since gdb fails to notice this 
 * failure, it sometimes tries to free a debug register, which is not 
 * allocated for it.
 */
int enable_debugreg(unsigned long old_dr7, unsigned long new_dr7)
{
	int i, dr_shift = 1UL;
	for (i = 0; i < DR_MAX; i++, dr_shift <<= 2) {
		if ((old_dr7 ^ new_dr7) & dr_shift) {
			if (new_dr7 & dr_shift)
				dr_alloc(i, DR_ALLOC_LOCAL);
			else
				dr_free(i);
			return 0;
		}
	}
	return -1;
}

EXPORT_SYMBOL(dr_alloc);
EXPORT_SYMBOL(dr_free);

/*
 * PowerPC hash table management proc entry.  Will show information
 * about the current hash table and will allow changes to it.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/sysctl.h>
#include <linux/ctype.h>
#include <linux/threads.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/mmu.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/system.h>
#include <asm/reg.h>

static ssize_t ppc_htab_read(struct file * file, char __user * buf,
			     size_t count, loff_t *ppos);
static ssize_t ppc_htab_write(struct file * file, const char __user * buffer,
			      size_t count, loff_t *ppos);
static long long ppc_htab_lseek(struct file * file, loff_t offset, int orig);
int proc_dol2crvec(ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp);

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern unsigned long _SDR1;
extern unsigned long htab_reloads;
extern unsigned long htab_preloads;
extern unsigned long htab_evicts;
extern unsigned long pte_misses;
extern unsigned long pte_errors;
extern unsigned int primary_pteg_full;
extern unsigned int htab_hash_searches;

struct file_operations ppc_htab_operations = {
        .llseek =       ppc_htab_lseek,
        .read =         ppc_htab_read,
        .write =        ppc_htab_write,
};

static char *pmc1_lookup(unsigned long mmcr0)
{
	switch ( mmcr0 & (0x7f<<7) )
	{
	case 0x0:
		return "none";
	case MMCR0_PMC1_CYCLES:
		return "cycles";
	case MMCR0_PMC1_ICACHEMISS:
		return "ic miss";
	case MMCR0_PMC1_DTLB:
		return "dtlb miss";
	default:
		return "unknown";
	}
}

static char *pmc2_lookup(unsigned long mmcr0)
{
	switch ( mmcr0 & 0x3f )
	{
	case 0x0:
		return "none";
	case MMCR0_PMC2_CYCLES:
		return "cycles";
	case MMCR0_PMC2_DCACHEMISS:
		return "dc miss";
	case MMCR0_PMC2_ITLB:
		return "itlb miss";
	case MMCR0_PMC2_LOADMISSTIME:
		return "load miss time";
	default:
		return "unknown";
	}
}

/*
 * print some useful info about the hash table.  This function
 * is _REALLY_ slow (see the nested for loops below) but nothing
 * in here should be really timing critical. -- Cort
 */
static ssize_t ppc_htab_read(struct file * file, char __user * buf,
			     size_t count, loff_t *ppos)
{
	unsigned long mmcr0 = 0, pmc1 = 0, pmc2 = 0;
	int n = 0;
#ifdef CONFIG_PPC_STD_MMU
	unsigned int kptes = 0, uptes = 0;
	PTE *ptr;
#endif /* CONFIG_PPC_STD_MMU */
	char buffer[512];

	if (count < 0)
		return -EINVAL;

	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_604_PERF_MON) {
		mmcr0 = mfspr(SPRN_MMCR0);
		pmc1 = mfspr(SPRN_PMC1);
		pmc2 = mfspr(SPRN_PMC2);
		n += sprintf( buffer + n,
			      "604 Performance Monitoring\n"
			      "MMCR0\t\t: %08lx %s%s ",
			      mmcr0,
			      ( mmcr0>>28 & 0x2 ) ? "(user mode counted)" : "",
			      ( mmcr0>>28 & 0x4 ) ? "(kernel mode counted)" : "");
		n += sprintf( buffer + n,
			      "\nPMC1\t\t: %08lx (%s)\n"
			      "PMC2\t\t: %08lx (%s)\n",
			      pmc1, pmc1_lookup(mmcr0),
			      pmc2, pmc2_lookup(mmcr0));
	}

#ifdef CONFIG_PPC_STD_MMU
	/* if we don't have a htab */
	if ( Hash_size == 0 )
	{
		n += sprintf( buffer + n, "No Hash Table used\n");
		goto return_string;
	}

	for (ptr = Hash; ptr < Hash_end; ptr++) {
		unsigned int mctx, vsid;

		if (!ptr->v)
			continue;
		/* undo the esid skew */
		vsid = ptr->vsid;
		mctx = ((vsid - (vsid & 0xf) * 0x111) >> 4) & 0xfffff;
		if (mctx == 0)
			kptes++;
		else
			uptes++;
	}

	n += sprintf( buffer + n,
		      "PTE Hash Table Information\n"
		      "Size\t\t: %luKb\n"
		      "Buckets\t\t: %lu\n"
 		      "Address\t\t: %08lx\n"
		      "Entries\t\t: %lu\n"
		      "User ptes\t: %u\n"
		      "Kernel ptes\t: %u\n"
		      "Percent full\t: %lu%%\n",
                      (unsigned long)(Hash_size>>10),
		      (Hash_size/(sizeof(PTE)*8)),
		      (unsigned long)Hash,
		      Hash_size/sizeof(PTE),
                      uptes,
		      kptes,
		      ((kptes+uptes)*100) / (Hash_size/sizeof(PTE))
		);

	n += sprintf( buffer + n,
		      "Reloads\t\t: %lu\n"
		      "Preloads\t: %lu\n"
		      "Searches\t: %u\n"
		      "Overflows\t: %u\n"
		      "Evicts\t\t: %lu\n",
		      htab_reloads, htab_preloads, htab_hash_searches,
		      primary_pteg_full, htab_evicts);
return_string:
#endif /* CONFIG_PPC_STD_MMU */

	n += sprintf( buffer + n,
		      "Non-error misses: %lu\n"
		      "Error misses\t: %lu\n",
		      pte_misses, pte_errors);
	if (*ppos >= strlen(buffer))
		return 0;
	if (n > strlen(buffer) - *ppos)
		n = strlen(buffer) - *ppos;
	if (n > count)
		n = count;
	if (copy_to_user(buf, buffer + *ppos, n))
		return -EFAULT;
	*ppos += n;
	return n;
}

/*
 * Allow user to define performance counters and resize the hash table
 */
static ssize_t ppc_htab_write(struct file * file, const char __user * ubuffer,
			      size_t count, loff_t *ppos)
{
#ifdef CONFIG_PPC_STD_MMU
	unsigned long tmp;
	char buffer[16];

	if ( current->uid != 0 )
		return -EACCES;
	if (strncpy_from_user(buffer, ubuffer, 15))
		return -EFAULT;
	buffer[15] = 0;

	/* don't set the htab size for now */
	if ( !strncmp( buffer, "size ", 5) )
		return -EBUSY;

	if ( !strncmp( buffer, "reset", 5) )
	{
		if (cur_cpu_spec[0]->cpu_features & CPU_FTR_604_PERF_MON) {
			/* reset PMC1 and PMC2 */
			mtspr(SPRN_PMC1, 0);
			mtspr(SPRN_PMC2, 0);
		}
		htab_reloads = 0;
		htab_evicts = 0;
		pte_misses = 0;
		pte_errors = 0;
	}

	/* Everything below here requires the performance monitor feature. */
	if ( !cur_cpu_spec[0]->cpu_features & CPU_FTR_604_PERF_MON )
		return count;

	/* turn off performance monitoring */
	if ( !strncmp( buffer, "off", 3) )
	{
		mtspr(SPRN_MMCR0, 0);
		mtspr(SPRN_PMC1, 0);
		mtspr(SPRN_PMC2, 0);
	}

	if ( !strncmp( buffer, "user", 4) )
	{
		/* setup mmcr0 and clear the correct pmc */
		tmp = (mfspr(SPRN_MMCR0) & ~(0x60000000)) | 0x20000000;
		mtspr(SPRN_MMCR0, tmp);
		mtspr(SPRN_PMC1, 0);
		mtspr(SPRN_PMC2, 0);
	}

	if ( !strncmp( buffer, "kernel", 6) )
	{
		/* setup mmcr0 and clear the correct pmc */
		tmp = (mfspr(SPRN_MMCR0) & ~(0x60000000)) | 0x40000000;
		mtspr(SPRN_MMCR0, tmp);
		mtspr(SPRN_PMC1, 0);
		mtspr(SPRN_PMC2, 0);
	}

	/* PMC1 values */
	if ( !strncmp( buffer, "dtlb", 4) )
	{
		/* setup mmcr0 and clear the correct pmc */
		tmp = (mfspr(SPRN_MMCR0) & ~(0x7F << 7)) | MMCR0_PMC1_DTLB;
		mtspr(SPRN_MMCR0, tmp);
		mtspr(SPRN_PMC1, 0);
	}

	if ( !strncmp( buffer, "ic miss", 7) )
	{
		/* setup mmcr0 and clear the correct pmc */
		tmp = (mfspr(SPRN_MMCR0) & ~(0x7F<<7)) | MMCR0_PMC1_ICACHEMISS;
		mtspr(SPRN_MMCR0, tmp);
		mtspr(SPRN_PMC1, 0);
	}

	/* PMC2 values */
	if ( !strncmp( buffer, "load miss time", 14) )
	{
		/* setup mmcr0 and clear the correct pmc */
	       asm volatile(
		       "mfspr %0,%1\n\t"     /* get current mccr0 */
		       "rlwinm %0,%0,0,0,31-6\n\t"  /* clear bits [26-31] */
		       "ori   %0,%0,%2 \n\t" /* or in mmcr0 settings */
		       "mtspr %1,%0 \n\t"    /* set new mccr0 */
		       "mtspr %3,%4 \n\t"    /* reset the pmc */
		       : "=r" (tmp)
		       : "i" (SPRN_MMCR0),
		       "i" (MMCR0_PMC2_LOADMISSTIME),
		       "i" (SPRN_PMC2),  "r" (0) );
	}

	if ( !strncmp( buffer, "itlb", 4) )
	{
		/* setup mmcr0 and clear the correct pmc */
	       asm volatile(
		       "mfspr %0,%1\n\t"     /* get current mccr0 */
		       "rlwinm %0,%0,0,0,31-6\n\t"  /* clear bits [26-31] */
		       "ori   %0,%0,%2 \n\t" /* or in mmcr0 settings */
		       "mtspr %1,%0 \n\t"    /* set new mccr0 */
		       "mtspr %3,%4 \n\t"    /* reset the pmc */
		       : "=r" (tmp)
		       : "i" (SPRN_MMCR0), "i" (MMCR0_PMC2_ITLB),
		       "i" (SPRN_PMC2),  "r" (0) );
	}

	if ( !strncmp( buffer, "dc miss", 7) )
	{
		/* setup mmcr0 and clear the correct pmc */
	       asm volatile(
		       "mfspr %0,%1\n\t"     /* get current mccr0 */
		       "rlwinm %0,%0,0,0,31-6\n\t"  /* clear bits [26-31] */
		       "ori   %0,%0,%2 \n\t" /* or in mmcr0 settings */
		       "mtspr %1,%0 \n\t"    /* set new mccr0 */
		       "mtspr %3,%4 \n\t"    /* reset the pmc */
		       : "=r" (tmp)
		       : "i" (SPRN_MMCR0), "i" (MMCR0_PMC2_DCACHEMISS),
		       "i" (SPRN_PMC2),  "r" (0) );
	}

	return count;
#else /* CONFIG_PPC_STD_MMU */
	return 0;
#endif /* CONFIG_PPC_STD_MMU */
}


static long long
ppc_htab_lseek(struct file * file, loff_t offset, int orig)
{
    long long ret = -EINVAL;

    lock_kernel();
    switch (orig) {
    case 0:
	file->f_pos = offset;
	ret = file->f_pos;
	break;
    case 1:
	file->f_pos += offset;
	ret = file->f_pos;
    }
    unlock_kernel();
    return ret;
}

int proc_dol2crvec(ctl_table *table, int write, struct file *filp,
		  void __user *buffer_arg, size_t *lenp)
{
	int vleft, first=1, len, left, val;
	char __user *buffer = (char __user *) buffer_arg;
	#define TMPBUFLEN 256
	char buf[TMPBUFLEN], *p;
	static const char *sizestrings[4] = {
		"2MB", "256KB", "512KB", "1MB"
	};
	static const char *clockstrings[8] = {
		"clock disabled", "+1 clock", "+1.5 clock", "reserved(3)",
		"+2 clock", "+2.5 clock", "+3 clock", "reserved(7)"
	};
	static const char *typestrings[4] = {
		"flow-through burst SRAM", "reserved SRAM",
		"pipelined burst SRAM", "pipelined late-write SRAM"
	};
	static const char *holdstrings[4] = {
		"0.5", "1.0", "(reserved2)", "(reserved3)"
	};

	if (!(cur_cpu_spec[0]->cpu_features & CPU_FTR_L2CR))
		return -EFAULT;

	if ( /*!table->maxlen ||*/ (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}

	vleft = table->maxlen / sizeof(int);
	left = *lenp;

	for (; left /*&& vleft--*/; first=0) {
		if (write) {
			while (left) {
				char c;
				if(get_user(c, buffer))
					return -EFAULT;
				if (!isspace(c))
					break;
				left--;
				buffer++;
			}
			if (!left)
				break;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			if(copy_from_user(buf, buffer, len))
				return -EFAULT;
			buf[len] = 0;
			p = buf;
			if (*p < '0' || *p > '9')
				break;
			val = simple_strtoul(p, &p, 0);
			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			buffer += len;
			left -= len;
			_set_L2CR(val);
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
			val = _get_L2CR();
			p += sprintf(p, "0x%08x: ", val);
			p += sprintf(p, " %s", (val >> 31) & 1 ? "enabled" :
				     	"disabled");
			p += sprintf(p, ", %sparity", (val>>30)&1 ? "" : "no ");
			p += sprintf(p, ", %s", sizestrings[(val >> 28) & 3]);
			p += sprintf(p, ", %s", clockstrings[(val >> 25) & 7]);
			p += sprintf(p, ", %s", typestrings[(val >> 23) & 2]);
			p += sprintf(p, "%s", (val>>22)&1 ? ", data only" : "");
			p += sprintf(p, "%s", (val>>20)&1 ? ", ZZ enabled": "");
			p += sprintf(p, ", %s", (val>>19)&1 ? "write-through" :
					"copy-back");
			p += sprintf(p, "%s", (val>>18)&1 ? ", testing" : "");
			p += sprintf(p, ", %sns hold",holdstrings[(val>>16)&3]);
			p += sprintf(p, "%s", (val>>15)&1 ? ", DLL slow" : "");
			p += sprintf(p, "%s", (val>>14)&1 ? ", diff clock" :"");
			p += sprintf(p, "%s", (val>>13)&1 ? ", DLL bypass" :"");

			p += sprintf(p,"\n");

			len = strlen(buf);
			if (len > left)
				len = left;
			if (copy_to_user(buffer, buf, len))
				return -EFAULT;
			left -= len;
			buffer += len;
			break;
		}
	}

	if (!write && !first && left) {
		if(put_user('\n', (char *) buffer))
			return -EFAULT;
		left--, buffer++;
	}
	if (write) {
		p = (char *) buffer;
		while (left) {
			char c;
			if(get_user(c, p++))
				return -EFAULT;
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	filp->f_pos += *lenp;
	return 0;
}

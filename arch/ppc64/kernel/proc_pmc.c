/*
 * proc_pmc.c
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen IBM Corporation
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */


/* Change Activity:
 * 2001       : mikec    : Created
 * 2001/06/05 : engebret : Software event count support.
 * End Change Activity 
 */

#include <linux/proc_fs.h>
#include <linux/spinlock.h>

#include <asm/proc_fs.h>
#include <asm/paca.h>
#include <asm/iSeries/ItLpPaca.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/iSeries/HvCallXm.h>
#include <asm/iSeries/IoHriMainStore.h>
#include <asm/processor.h>
#include <asm/time.h>
#include <asm/iSeries/LparData.h>
#include <asm/pmc.h>
#include <asm/uaccess.h>
#include <asm/naca.h>
#include <asm/rtas.h>


static int proc_pmc_control_mode = 0;

static struct proc_dir_entry *proc_ppc64_root = NULL;
static struct proc_dir_entry *proc_ppc64_pmc_root = NULL;
static struct proc_dir_entry *proc_ppc64_pmc_system_root = NULL;
static struct proc_dir_entry *proc_ppc64_pmc_cpu_root[NR_CPUS] = {NULL, };

static spinlock_t proc_ppc64_lock;
int proc_ppc64_pmc_find_file(void *data);
int proc_ppc64_pmc_read(char *page, char **start, off_t off,
			int count, int *eof, char *buffer);
int proc_ppc64_pmc_stab_read(char *page, char **start, off_t off,
			     int count, int *eof, void *data);
int proc_ppc64_pmc_htab_read(char *page, char **start, off_t off,
			     int count, int *eof, void *data);
int proc_ppc64_pmc_hw_read(char *page, char **start, off_t off, 
			   int count, int *eof, void *data);

static struct proc_dir_entry *pmc_proc_root = NULL;

int proc_get_lpevents( char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_reset_lpevents( struct file *file, const char *buffer, unsigned long count, void *data);

int proc_get_titanTod( char *page, char **start, off_t off, int count, int *eof, void *data);

int proc_pmc_get_control( char *page, char **start, off_t off, int count, int *eof, void *data);

int proc_pmc_set_control( struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_mmcr0( struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_mmcr1( struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_mmcra( struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_pmc1(  struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_pmc2(  struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_pmc3(  struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_pmc4(  struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_pmc5(  struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_pmc6(  struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_pmc7(  struct file *file, const char *buffer, unsigned long count, void *data);
int proc_pmc_set_pmc8(  struct file *file, const char *buffer, unsigned long count, void *data);


void proc_ppc64_init(void)
{
	unsigned long i;
	struct proc_dir_entry *ent = NULL;
	char buf[256];

	printk("proc_ppc64: Creating /proc/ppc64/pmc\n");

	/*
	 * Create the root, system, and cpu directories as follows:
	 *   /proc/ppc64/pmc/system 
	 *   /proc/ppc64/pmc/cpu0 
	 */
	spin_lock(&proc_ppc64_lock);
	proc_ppc64_root = proc_mkdir("ppc64", 0);
	if (!proc_ppc64_root) return;
	spin_unlock(&proc_ppc64_lock);

	/* Placeholder for rtas interfaces. */
	rtas_proc_dir = proc_mkdir("rtas", proc_ppc64_root);


	proc_ppc64_pmc_root = proc_mkdir("pmc", proc_ppc64_root);

	proc_ppc64_pmc_system_root = proc_mkdir("system", proc_ppc64_pmc_root);
	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_online(i)) {
			sprintf(buf, "cpu%ld", i); 
			proc_ppc64_pmc_cpu_root[i] =
				proc_mkdir(buf, proc_ppc64_pmc_root);
		}
	}


	/* Create directories for the software counters. */
	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_online(i))
			continue;
		ent = create_proc_entry("stab", S_IRUGO | S_IWUSR, 
					proc_ppc64_pmc_cpu_root[i]);
		if (ent) {
			ent->nlink = 1;
			ent->data = (void *)proc_ppc64_pmc_cpu_root[i];
			ent->read_proc = (void *)proc_ppc64_pmc_stab_read;
			ent->write_proc = (void *)proc_ppc64_pmc_stab_read;
		}

		ent = create_proc_entry("htab", S_IRUGO | S_IWUSR, 
					proc_ppc64_pmc_cpu_root[i]);
		if (ent) {
			ent->nlink = 1;
			ent->data = (void *)proc_ppc64_pmc_cpu_root[i];
			ent->read_proc = (void *)proc_ppc64_pmc_htab_read;
			ent->write_proc = (void *)proc_ppc64_pmc_htab_read;
		}
	}

	ent = create_proc_entry("stab", S_IRUGO | S_IWUSR, 
				proc_ppc64_pmc_system_root);
	if (ent) {
		ent->nlink = 1;
		ent->data = (void *)proc_ppc64_pmc_system_root;
		ent->read_proc = (void *)proc_ppc64_pmc_stab_read;
		ent->write_proc = (void *)proc_ppc64_pmc_stab_read;
	}

	ent = create_proc_entry("htab", S_IRUGO | S_IWUSR, 
				proc_ppc64_pmc_system_root);
	if (ent) {
		ent->nlink = 1;
		ent->data = (void *)proc_ppc64_pmc_system_root;
		ent->read_proc = (void *)proc_ppc64_pmc_htab_read;
		ent->write_proc = (void *)proc_ppc64_pmc_htab_read;
	}

	/* Create directories for the hardware counters. */
	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_online(i))
			continue;
		ent = create_proc_entry("hardware", S_IRUGO | S_IWUSR, 
					proc_ppc64_pmc_cpu_root[i]);
		if (ent) {
			ent->nlink = 1;
			ent->data = (void *)proc_ppc64_pmc_cpu_root[i];
			ent->read_proc = (void *)proc_ppc64_pmc_hw_read;
			ent->write_proc = (void *)proc_ppc64_pmc_hw_read;
		}
	}

	ent = create_proc_entry("hardware", S_IRUGO | S_IWUSR, 
				proc_ppc64_pmc_system_root);
	if (ent) {
		ent->nlink = 1;
		ent->data = (void *)proc_ppc64_pmc_system_root;
		ent->read_proc = (void *)proc_ppc64_pmc_hw_read;
		ent->write_proc = (void *)proc_ppc64_pmc_hw_read;
	}
}

/*
 * Find the requested 'file' given a proc token.
 *
 * Inputs: void * data: proc token
 * Output: int        : (0, ..., +N) = CPU number.
 *                      -1           = System.
 */
int proc_ppc64_pmc_find_file(void *data)
{
	int i;

	if ((unsigned long)data == 
	   (unsigned long) proc_ppc64_pmc_system_root) {
		return(-1); 
	} else {
		for (i = 0; i < NR_CPUS; i++) {
			if (!cpu_online(i))
				continue;
			if ((unsigned long)data ==
			   (unsigned long)proc_ppc64_pmc_cpu_root[i]) {
				return(i); 
			}
		}
	}

	/* On error, just default to a type of system. */
	printk("proc_ppc64_pmc_find_file: failed to find file token.\n"); 
	return(-1); 
}

int 
proc_ppc64_pmc_read(char *page, char **start, off_t off, 
		    int count, int *eof, char *buffer)
{
	int buffer_size, n;

	if (count < 0) return 0;

	if (buffer == NULL) {
		*eof = 1;
		return 0;
	}

	/* Check for read beyond EOF */
	buffer_size = n = strlen(buffer);
	if (off >= buffer_size) {
		*eof = 1;
		return 0;
	}
	if (n > (buffer_size - off)) n = buffer_size - off;

	/* Never return more than was requested */
	if (n > count) {
		n = count;
	} else {
		*eof = 1;
	}

	memcpy(page, buffer + off, n);

	*start = page;

	return n;
}

int 
proc_ppc64_pmc_stab_read(char *page, char **start, off_t off, 
			 int count, int *eof, void *data)
{
	int n, file;
	char *buffer = NULL;

	if (count < 0) return 0;
	spin_lock(&proc_ppc64_lock);

	/* Figure out which file is being request. */
	file = proc_ppc64_pmc_find_file(data);

	/* Update the counters and the text buffer representation. */
	buffer = ppc64_pmc_stab(file);

	/* Put the data into the requestor's buffer. */
	n = proc_ppc64_pmc_read(page, start, off, count, eof, buffer); 

	spin_unlock(&proc_ppc64_lock);
	return n;
}

int 
proc_ppc64_pmc_htab_read(char *page, char **start, off_t off, 
			 int count, int *eof, void *data)
{
	int n, file;
	char *buffer = NULL;

	if (count < 0) return 0;
	spin_lock(&proc_ppc64_lock);

	/* Figure out which file is being request. */
	file = proc_ppc64_pmc_find_file(data);

	/* Update the counters and the text buffer representation. */
	buffer = ppc64_pmc_htab(file);

	/* Put the data into the requestor's buffer. */
	n = proc_ppc64_pmc_read(page, start, off, count, eof, buffer);

	spin_unlock(&proc_ppc64_lock);
	return n;
}

int 
proc_ppc64_pmc_hw_read(char *page, char **start, off_t off, 
			     int count, int *eof, void *data)
{
	int n, file;
	char *buffer = NULL;

	if (count < 0) return 0;
	spin_lock(&proc_ppc64_lock);

	/* Figure out which file is being request. */
	file = proc_ppc64_pmc_find_file(data);

	/* Update the counters and the text buffer representation. */
	buffer = ppc64_pmc_hw(file);

	/* Put the data into the requestor's buffer. */
	n = proc_ppc64_pmc_read(page, start, off, count, eof, buffer);

	spin_unlock(&proc_ppc64_lock);
	return n;
}

/* 
 * DRENG the remainder of these functions still need work ...
 */
void pmc_proc_init(struct proc_dir_entry *iSeries_proc)
{
    struct proc_dir_entry *ent = NULL;

    ent = create_proc_entry("lpevents", S_IFREG|S_IRUGO, iSeries_proc);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_get_lpevents;
    ent->write_proc = proc_reset_lpevents;

    ent = create_proc_entry("titanTod", S_IFREG|S_IRUGO, iSeries_proc);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->size = 0;
    ent->read_proc = proc_get_titanTod;
    ent->write_proc = NULL;

    pmc_proc_root = proc_mkdir("pmc", iSeries_proc);
    if (!pmc_proc_root) return;

    ent = create_proc_entry("control", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_control;
    ent->write_proc = proc_pmc_set_control;

}

static int pmc_calc_metrics( char *page, char **start, off_t off, int count, int *eof, int len)
{
	if ( len <= off+count)
		*eof = 1;
	*start = page+off;
	len -= off;
	if ( len > count )
		len = count;
	if ( len < 0 )
		len = 0;
	return len;
}

static char * lpEventTypes[9] = {
	"Hypervisor\t\t",
	"Machine Facilities\t",
	"Session Manager\t",
	"SPD I/O\t\t",
	"Virtual Bus\t\t",
	"PCI I/O\t\t",
	"RIO I/O\t\t",
	"Virtual Lan\t\t",
	"Virtual I/O\t\t"
	};
	

int proc_get_lpevents
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	unsigned i;
	int len = 0;

	len += sprintf( page+len, "LpEventQueue 0\n" );
	len += sprintf( page+len, "  events processed:\t%lu\n",
			(unsigned long)xItLpQueue.xLpIntCount );
	for (i=0; i<9; ++i) {
		len += sprintf( page+len, "    %s %10lu\n",
			lpEventTypes[i],
			(unsigned long)xItLpQueue.xLpIntCountByType[i] );
	}
	len += sprintf( page+len, "\n  events processed by processor:\n" );
	for (i = 0; i < NR_CPUS; ++i) {
		if (cpu_online(i))
			len += sprintf( page+len, "    CPU%02d  %10u\n",
				i, paca[i].lpEvent_count );
	}

	return pmc_calc_metrics( page, start, off, count, eof, len );

}

int proc_reset_lpevents( struct file *file, const char *buffer, unsigned long count, void *data )
{
	return count;
}

static unsigned long startTitan = 0;
static unsigned long startTb = 0;


int proc_get_titanTod
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;
	unsigned long tb0, titan_tod;

	tb0 = get_tb();
	titan_tod = HvCallXm_loadTod();

	len += sprintf( page+len, "Titan\n" );
	len += sprintf( page+len, "  time base =          %016lx\n", tb0 );
	len += sprintf( page+len, "  titan tod =          %016lx\n", titan_tod );
	len += sprintf( page+len, "  xProcFreq =          %016x\n", xIoHriProcessorVpd[0].xProcFreq );
	len += sprintf( page+len, "  xTimeBaseFreq =      %016x\n", xIoHriProcessorVpd[0].xTimeBaseFreq );
	len += sprintf( page+len, "  tb_ticks_per_jiffy = %lu\n", tb_ticks_per_jiffy );
	len += sprintf( page+len, "  tb_ticks_per_usec  = %lu\n", tb_ticks_per_usec );

	if ( !startTitan ) {
		startTitan = titan_tod;
		startTb = tb0;
	}
	else {
		unsigned long titan_usec = (titan_tod - startTitan) >> 12;
		unsigned long tb_ticks = (tb0 - startTb);
		unsigned long titan_jiffies = titan_usec / (1000000/HZ);
		unsigned long titan_jiff_usec = titan_jiffies * (1000000/HZ);
		unsigned long titan_jiff_rem_usec = titan_usec - titan_jiff_usec;
		unsigned long tb_jiffies = tb_ticks / tb_ticks_per_jiffy;
		unsigned long tb_jiff_ticks = tb_jiffies * tb_ticks_per_jiffy;
		unsigned long tb_jiff_rem_ticks = tb_ticks - tb_jiff_ticks;
		unsigned long tb_jiff_rem_usec = tb_jiff_rem_ticks / tb_ticks_per_usec;
		unsigned long new_tb_ticks_per_jiffy = (tb_ticks * (1000000/HZ))/titan_usec;
		
		len += sprintf( page+len, "  titan elapsed = %lu uSec\n", titan_usec);
		len += sprintf( page+len, "  tb elapsed    = %lu ticks\n", tb_ticks);
		len += sprintf( page+len, "  titan jiffies = %lu.%04lu \n", titan_jiffies, titan_jiff_rem_usec );				
		len += sprintf( page+len, "  tb jiffies    = %lu.%04lu\n", tb_jiffies, tb_jiff_rem_usec );
		len += sprintf( page+len, "  new tb_ticks_per_jiffy = %lu\n", new_tb_ticks_per_jiffy );	

	}
	
	return pmc_calc_metrics( page, start, off, count, eof, len );
}
	
int proc_pmc_get_control
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;

	if ( proc_pmc_control_mode == PMC_CONTROL_CPI ) {
		unsigned long mach_cycles   = mfspr( PMC5 );
		unsigned long inst_complete = mfspr( PMC4 );
		unsigned long inst_dispatch = mfspr( PMC3 );
		unsigned long thread_active_run = mfspr( PMC1 );
		unsigned long thread_active  = mfspr( PMC2 );
		unsigned long cpi = 0;
		unsigned long cpithou = 0;
		unsigned long remain;
	
		if ( inst_complete ) {
			cpi = thread_active_run / inst_complete;
			remain = thread_active_run % inst_complete;
			if ( inst_complete > 1000000 ) 
				cpithou = remain / ( inst_complete / 1000 );
			else 
				cpithou = ( remain * 1000 ) / inst_complete;
		}
		len += sprintf( page+len, "PMC CPI Mode\nRaw Counts\n" );
		len += sprintf( page+len, "machine cycles           : %12lu\n", mach_cycles );
		len += sprintf( page+len, "thread active cycles     : %12lu\n\n", thread_active );

		len += sprintf( page+len, "instructions completed   : %12lu\n", inst_complete );
		len += sprintf( page+len, "instructions dispatched  : %12lu\n", inst_dispatch );
		len += sprintf( page+len, "thread active run cycles : %12lu\n", thread_active_run );

		len += sprintf( page+len, "thread active run cycles/instructions completed\n" );
		len += sprintf( page+len, "CPI = %lu.%03lu\n", cpi, cpithou );
		
	}
	else if ( proc_pmc_control_mode == PMC_CONTROL_TLB ) {
		len += sprintf( page+len, "PMC TLB Mode\n" );
		len += sprintf( page+len, "I-miss count             : %12lu\n", mfspr( PMC1 ) );
		len += sprintf( page+len, "I-miss latency           : %12lu\n", mfspr( PMC2 ) );
		len += sprintf( page+len, "D-miss count             : %12lu\n", mfspr( PMC3 ) );
		len += sprintf( page+len, "D-miss latency           : %12lu\n", mfspr( PMC4 ) );
		len += sprintf( page+len, "IERAT miss count         : %12lu\n", mfspr( PMC5 ) );
		len += sprintf( page+len, "D-reference count        : %12lu\n", mfspr( PMC6 ) );
		len += sprintf( page+len, "miss PTEs searched       : %12lu\n", mfspr( PMC7 ) );
		len += sprintf( page+len, "miss >8 PTEs searched    : %12lu\n", mfspr( PMC8 ) );
	}
	/* IMPLEMENT ME */
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

unsigned long proc_pmc_conv_int( const char *buf, unsigned count )
{
	const char * p;
	char b0, b1;
	unsigned v, multiplier, mult, i;
	unsigned long val;
	multiplier = 10;
	p = buf;
	if ( count >= 3 ) {
		b0 = buf[0];
		b1 = buf[1];
		if ( ( b0 == '0' ) &&
		     ( ( b1 == 'x' ) || ( b1 == 'X' ) ) ) {
			p = buf + 2;
			count -= 2;
			multiplier = 16;
		}
			
	}
	val = 0;
	for ( i=0; i<count; ++i ) {
		b0 = *p++;
		v = 0;
		mult = multiplier;
		if ( ( b0 >= '0' ) && ( b0 <= '9' ) ) 
			v = b0 - '0';
		else if ( multiplier == 16 ) {
			if ( ( b0 >= 'a' ) && ( b0 <= 'f' ) )
				v = b0 - 'a' + 10;
			else if ( ( b0 >= 'A' ) && ( b0 <= 'F' ) )
				v = b0 - 'A' + 10;
			else 
				mult = 1;
		}
		else
			mult = 1;
		val *= mult;
		val += v;
	}

	return val;

}

static inline void proc_pmc_stop(void)
{
	/* Freeze all counters, leave everything else alone */
	mtspr( MMCR0, mfspr( MMCR0 ) | 0x80000000 );
}

static inline void proc_pmc_start(void)
{
	/* Unfreeze all counters, leave everything else alone */
	mtspr( MMCR0, mfspr( MMCR0 ) & ~0x80000000 );

}

static inline void proc_pmc_reset(void)
{
	/* Clear all the PMCs to zeros 
	 * Assume a "stop" has already frozen the counters
	 * Clear all the PMCs
	 */
	mtspr( PMC1, 0 );
	mtspr( PMC2, 0 );
	mtspr( PMC3, 0 );
	mtspr( PMC4, 0 );
	mtspr( PMC5, 0 );
	mtspr( PMC6, 0 );
	mtspr( PMC7, 0 );
	mtspr( PMC8, 0 );

}

static inline void proc_pmc_cpi(void)
{
	/* Configure the PMC registers to count cycles and instructions */
	/* so we can compute cpi */
	/*
	 * MMCRA[30]    = 1     Don't count in wait state (CTRL[31]=0)
	 * MMCR0[6]     = 1     Freeze counters when any overflow
	 * MMCR0[19:25] = 0x01  PMC1 counts Thread Active Run Cycles
	 * MMCR0[26:31] = 0x05	PMC2 counts Thread Active Cycles
	 * MMCR1[0:4]   = 0x07	PMC3 counts Instructions Dispatched
	 * MMCR1[5:9]   = 0x03	PMC4 counts Instructions Completed
	 * MMCR1[10:14] = 0x06	PMC5 counts Machine Cycles
	 *
	 */

	proc_pmc_control_mode = PMC_CONTROL_CPI;
	
	/* Indicate to hypervisor that we are using the PMCs */
	get_paca()->xLpPacaPtr->xPMCRegsInUse = 1;

	/* Freeze all counters */
	mtspr( MMCR0, 0x80000000 );
	mtspr( MMCR1, 0x00000000 );
	
	/* Clear all the PMCs */
	mtspr( PMC1, 0 );
	mtspr( PMC2, 0 );
	mtspr( PMC3, 0 );
	mtspr( PMC4, 0 );
	mtspr( PMC5, 0 );
	mtspr( PMC6, 0 );
	mtspr( PMC7, 0 );
	mtspr( PMC8, 0 );

	/* Freeze counters in Wait State (CTRL[31]=0) */
	mtspr( MMCRA, 0x00000002 );

	/* PMC3<-0x07, PMC4<-0x03, PMC5<-0x06 */
	mtspr( MMCR1, 0x38cc0000 );

	mb();
	
	/* PMC1<-0x01, PMC2<-0x05
	 * Start all counters
	 */
	mtspr( MMCR0, 0x02000045 );
	
}

static inline void proc_pmc_tlb(void)
{
	/* Configure the PMC registers to count tlb misses  */
	/*
	 * MMCR0[6]     = 1     Freeze counters when any overflow
	 * MMCR0[19:25] = 0x55  Group count
	 *   PMC1 counts  I misses
	 *   PMC2 counts  I miss duration (latency)
	 *   PMC3 counts  D misses
	 *   PMC4 counts  D miss duration (latency)
	 *   PMC5 counts  IERAT misses
	 *   PMC6 counts  D references (including PMC7)
	 *   PMC7 counts  miss PTEs searched
	 *   PMC8 counts  miss >8 PTEs searched
	 *   
	 */

	proc_pmc_control_mode = PMC_CONTROL_TLB;
	
	/* Indicate to hypervisor that we are using the PMCs */
	get_paca()->xLpPacaPtr->xPMCRegsInUse = 1;

	/* Freeze all counters */
	mtspr( MMCR0, 0x80000000 );
	mtspr( MMCR1, 0x00000000 );
	
	/* Clear all the PMCs */
	mtspr( PMC1, 0 );
	mtspr( PMC2, 0 );
	mtspr( PMC3, 0 );
	mtspr( PMC4, 0 );
	mtspr( PMC5, 0 );
	mtspr( PMC6, 0 );
	mtspr( PMC7, 0 );
	mtspr( PMC8, 0 );

	mtspr( MMCRA, 0x00000000 );

	mb();
	
	/* PMC1<-0x55
	 * Start all counters
	 */
	mtspr( MMCR0, 0x02001540 );
	
}

int proc_pmc_set_control( struct file *file, const char *buffer, unsigned long count, void *data )
{
	if      ( ! strncmp( buffer, "stop", 4 ) )
		proc_pmc_stop();
	else if ( ! strncmp( buffer, "start", 5 ) )
		proc_pmc_start();
	else if ( ! strncmp( buffer, "reset", 5 ) )
		proc_pmc_reset();
	else if ( ! strncmp( buffer, "cpi", 3 ) )
		proc_pmc_cpi();
	else if ( ! strncmp( buffer, "tlb", 3 ) )
		proc_pmc_tlb();
	
	/* IMPLEMENT ME */
	return count;
}

int proc_pmc_set_mmcr0( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	v = v & ~0x04000000;	/* Don't allow interrupts for now */
	if ( v & ~0x80000000 ) 	/* Inform hypervisor we are using PMCs */
		get_paca()->xLpPacaPtr->xPMCRegsInUse = 1;
	else
		get_paca()->xLpPacaPtr->xPMCRegsInUse = 0;
	mtspr( MMCR0, v );
	
	return count;	
}

int proc_pmc_set_mmcr1( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	mtspr( MMCR1, v );

	return count;
}

int proc_pmc_set_mmcra( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	v = v & ~0x00008000;	/* Don't allow interrupts for now */
	mtspr( MMCRA, v );

	return count;
}


int proc_pmc_set_pmc1( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	mtspr( PMC1, v );

	return count;
}

int proc_pmc_set_pmc2( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	mtspr( PMC2, v );

	return count;
}

int proc_pmc_set_pmc3( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	mtspr( PMC3, v );

	return count;
}

int proc_pmc_set_pmc4( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	mtspr( PMC4, v );

	return count;
}

int proc_pmc_set_pmc5( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	mtspr( PMC5, v );

	return count;
}

int proc_pmc_set_pmc6( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	mtspr( PMC6, v );

	return count;
}

int proc_pmc_set_pmc7( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	mtspr( PMC7, v );

	return count;
}

int proc_pmc_set_pmc8( struct file *file, const char *buffer, unsigned long count, void *data )
{
	unsigned long v;
	v = proc_pmc_conv_int( buffer, count );
	mtspr( PMC8, v );

	return count;
}


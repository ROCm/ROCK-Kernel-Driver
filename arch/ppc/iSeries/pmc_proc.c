/*
 * pmc_proc.c
 * Copyright (C) 2001 Mike Corrigan  IBM Corporation
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


/* Change Activity: */
/* End Change Activity */

#ifndef _PMC_PROC_H
#include <asm/iSeries/pmc_proc.h>
#endif

#include <asm/iSeries/Paca.h>
#include <asm/iSeries/ItLpPaca.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/processor.h>

#define MMCR0 795
#define MMCR1 798
#define MMCRA 786
#define PMC1  787
#define PMC2  788
#define PMC3  789
#define PMC4  790
#define PMC5  791
#define PMC6  792
#define PMC7  793
#define PMC8  794

static int proc_pmc_control_mode = 0;
#define PMC_CONTROL_CPI 1
#define PMC_CONTROL_TLB 2

static struct proc_dir_entry *pmc_proc_root = NULL;

int proc_get_lpevents( char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_reset_lpevents( struct file *file, const char *buffer, unsigned long count, void *data);

int proc_pmc_get_control( char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_mmcr0( char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_mmcr1( char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_mmcra( char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_pmc1(  char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_pmc2(  char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_pmc3(  char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_pmc4(  char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_pmc5(  char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_pmc6(  char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_pmc7(  char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_pmc_get_pmc8(  char *page, char **start, off_t off, int count, int *eof, void *data);

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

void pmc_proc_init(struct proc_dir_entry *iSeries_proc)
{
    struct proc_dir_entry *ent = NULL;

    ent = create_proc_entry("lpevents", S_IFREG|S_IRUGO, iSeries_proc);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_get_lpevents;
    ent->write_proc = proc_reset_lpevents;
    
    pmc_proc_root = proc_mkdir("pmc", iSeries_proc);
    if (!pmc_proc_root) return;

    ent = create_proc_entry("control", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_control;
    ent->write_proc = proc_pmc_set_control;
    
    ent = create_proc_entry("mmcr0", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_mmcr0;
    ent->write_proc = proc_pmc_set_mmcr0;

    ent = create_proc_entry("mmcr1", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_mmcr1;
    ent->write_proc = proc_pmc_set_mmcr1;

    ent = create_proc_entry("mmcra", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_mmcra;
    ent->write_proc = proc_pmc_set_mmcra;

    ent = create_proc_entry("pmc1", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_pmc1;
    ent->write_proc = proc_pmc_set_pmc1;

    ent = create_proc_entry("pmc2", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_pmc2;
    ent->write_proc = proc_pmc_set_pmc2;

    ent = create_proc_entry("pmc3", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_pmc3;
    ent->write_proc = proc_pmc_set_pmc3;

    ent = create_proc_entry("pmc4", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_pmc4;
    ent->write_proc = proc_pmc_set_pmc4;

    ent = create_proc_entry("pmc5", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_pmc5;
    ent->write_proc = proc_pmc_set_pmc5;

    ent = create_proc_entry("pmc6", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_pmc6;
    ent->write_proc = proc_pmc_set_pmc6;

    ent = create_proc_entry("pmc7", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_pmc7;
    ent->write_proc = proc_pmc_set_pmc7;

    ent = create_proc_entry("pmc8", S_IFREG|S_IRUSR|S_IWUSR, pmc_proc_root);
    if (!ent) return;
    ent->nlink = 1;
    ent->data = (void *)0;
    ent->read_proc = proc_pmc_get_pmc8;
    ent->write_proc = proc_pmc_set_pmc8;


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
	return pmc_calc_metrics( page, start, off, count, eof, len );

}

int proc_reset_lpevents( struct file *file, const char *buffer, unsigned long count, void *data )
{
	return count;
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
		len += sprintf( page+len, "I-miss count             : %12u\n", mfspr( PMC1 ) );
		len += sprintf( page+len, "I-miss latency           : %12u\n", mfspr( PMC2 ) );
		len += sprintf( page+len, "D-miss count             : %12u\n", mfspr( PMC3 ) );
		len += sprintf( page+len, "D-miss latency           : %12u\n", mfspr( PMC4 ) );
		len += sprintf( page+len, "IERAT miss count         : %12u\n", mfspr( PMC5 ) );
		len += sprintf( page+len, "D-reference count        : %12u\n", mfspr( PMC6 ) );
		len += sprintf( page+len, "miss PTEs searched       : %12u\n", mfspr( PMC7 ) );
		len += sprintf( page+len, "miss >8 PTEs searched    : %12u\n", mfspr( PMC8 ) );
	}
	/* IMPLEMENT ME */
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_mmcr0
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(MMCR0) );
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_mmcr1
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(MMCR1) );
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_mmcra
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(MMCRA) );
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_pmc1
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(PMC1) );
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_pmc2
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(PMC2) );
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_pmc3
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(PMC3) );
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_pmc4
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(PMC4) );
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_pmc5
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(PMC5) );
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_pmc6
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(PMC6) );
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_pmc7
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(PMC7) );
	return pmc_calc_metrics( page, start, off, count, eof, len );
}

int proc_pmc_get_pmc8
(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = sprintf( page, "0x%08x", mfspr(PMC8) );
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
	// Freeze all counters, leave everything else alone
	mtspr( MMCR0, mfspr( MMCR0 ) | 0x80000000 );
}

static inline void proc_pmc_start(void)
{
	// Unfreeze all counters, leave everything else alone
	mtspr( MMCR0, mfspr( MMCR0 ) & ~0x80000000 );

}

static inline void proc_pmc_reset(void)
{
	// Clear all the PMCs to zero
	// Assume a "stop" has already frozen the counters
	// Clear all the PMCs
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
	
	// Indicate to hypervisor that we are using the PMCs
	((struct Paca *)mfspr(SPRG1))->xLpPacaPtr->xPMCRegsInUse = 1;

	// Freeze all counters
	mtspr( MMCR0, 0x80000000 );
	mtspr( MMCR1, 0x00000000 );
	
	// Clear all the PMCs
	mtspr( PMC1, 0 );
	mtspr( PMC2, 0 );
	mtspr( PMC3, 0 );
	mtspr( PMC4, 0 );
	mtspr( PMC5, 0 );
	mtspr( PMC6, 0 );
	mtspr( PMC7, 0 );
	mtspr( PMC8, 0 );

	// Freeze counters in Wait State (CTRL[31]=0)
	mtspr( MMCRA, 0x00000002 );

	// PMC3<-0x07, PMC4<-0x03, PMC5<-0x06
	mtspr( MMCR1, 0x38cc0000 );

	mb();
	
	// PMC1<-0x01, PMC2<-0x05
	// Start all counters
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
	
	// Indicate to hypervisor that we are using the PMCs
	((struct Paca *)mfspr(SPRG1))->xLpPacaPtr->xPMCRegsInUse = 1;

	// Freeze all counters
	mtspr( MMCR0, 0x80000000 );
	mtspr( MMCR1, 0x00000000 );
	
	// Clear all the PMCs
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
	
	// PMC1<-0x55
	// Start all counters
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
		((struct Paca *)mfspr(SPRG1))->xLpPacaPtr->xPMCRegsInUse = 1;
	else
		((struct Paca *)mfspr(SPRG1))->xLpPacaPtr->xPMCRegsInUse = 0;
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



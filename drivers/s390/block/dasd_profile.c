#include <linux/mm.h>

#include <linux/dasd.h>

#include "dasd_types.h"

#define PRINTK_HEADER "dasd_profile:"

static long dasd_io_reqs=0; /* number of requests processed at all */
static long dasd_io_secs[16]; /* histogram of request's sizes */
static long dasd_io_times[16]; /* histogram of requests's times */
static long dasd_io_timps[16]; /* histogram of requests's times per sector */
static long dasd_io_time1[16]; /* histogram of time from build to start */
static long dasd_io_time2[16]; /* histogram of time from start to irq */
static long dasd_io_time2ps[16]; /* histogram of time from start to irq */
static long dasd_io_time3[16]; /* histogram of time from irq to end */

void
dasd_profile_add ( cqr_t *cqr )
{
	int ind;
	long strtime,irqtime,endtime,tottime;
	long tottimeps,sectors;
	long help;
	if ( ! cqr -> req ) 
	  return;
	sectors = cqr -> req -> nr_sectors;
	strtime = ((cqr->startclk - cqr->buildclk) >> 12);
	irqtime = ((cqr->stopclk - cqr->startclk) >> 12);
	endtime = ((cqr->endclk - cqr->stopclk) >> 12);
	tottime = ((cqr->endclk - cqr->buildclk) >> 12);
	tottimeps = tottime / sectors;

	if (! dasd_io_reqs ++){
	  for ( ind = 0; ind < 16; ind ++) {
		dasd_io_secs[ind] = 0;
		dasd_io_times[ind]=0;
		dasd_io_timps[ind]=0;
		dasd_io_time1[ind]=0;
		dasd_io_time2[ind]=0;
		dasd_io_time2ps[ind]=0;
		dasd_io_time3[ind]=0;
	  }
	};
	
	for ( ind = 0, help = sectors >> 3; 
	      ind < 15 && help; 
	      help = help >> 1,ind ++);
	dasd_io_secs[ind] ++;

	for ( ind = 0, help = tottime >> 3; 
	      ind < 15 && help; 
	      help = help >> 1,ind ++);
	dasd_io_times[ind] ++;

	for ( ind = 0, help = tottimeps >> 3; 
	      ind < 15 && help; 
	      help = help >> 1,ind ++);
	dasd_io_timps[ind] ++;

	for ( ind = 0, help = strtime >> 3; 
	      ind < 15 && help; 
	      help = help >> 1,ind ++);
	dasd_io_time1[ind] ++;

	for ( ind = 0, help = irqtime >> 3; 
	      ind < 15 && help; 
	      help = help >> 1,ind ++);
	dasd_io_time2[ind] ++;

	for ( ind = 0, help = (irqtime/sectors) >> 3; 
	      ind < 15 && help; 
	      help = help >> 1,ind ++);
	dasd_io_time2ps[ind] ++;

	for ( ind = 0, help = endtime >> 3; 
	      ind < 15 && help; 
	      help = help >> 1,ind ++);
	dasd_io_time3[ind] ++;
}

int 
dasd_proc_read_statistics ( char * buf, char **start, 
			    off_t off, int len, int d)
{
	int i;
	int shift, help;
	
	for ( shift = 0, help = dasd_io_reqs; 
	      help > 8192; 
	      help = help >> 1,shift ++);
	len = sprintf ( buf, "%ld dasd I/O requests\n", dasd_io_reqs); 
	len += sprintf ( buf+len, "__<4 ___8 __16 __32 __64 _128 _256 _512 __1k __2k __4k __8k _16k _32k _64k >64k\n");
	len += sprintf ( buf+len, "Histogram of sizes (512B secs)\n");
	for ( i = 0; i < 16; i ++) {
		len += sprintf ( buf+len, "%4ld ",dasd_io_secs[i] >> shift );
	}
	len += sprintf ( buf+len, "\n");
	len += sprintf ( buf+len, "Histogram of I/O times\n");
	for ( i = 0; i < 16; i ++) {
		len += sprintf ( buf+len, "%4ld ",dasd_io_times[i] >> shift );
	}
	len += sprintf ( buf+len, "\n");
	len += sprintf ( buf+len, "Histogram of I/O times per sector\n");
	for ( i = 0; i < 16; i ++) {
		len += sprintf ( buf+len, "%4ld ",dasd_io_timps[i] >> shift );
	}
	len += sprintf ( buf+len, "\n");
	len += sprintf ( buf+len, "Histogram of I/O time till ssch\n");
	for ( i = 0; i < 16; i ++) {
		len += sprintf ( buf+len, "%4ld ",dasd_io_time1[i] >> shift );
	}
	len += sprintf ( buf+len, "\n");
	len += sprintf ( buf+len, "Histogram of I/O time between ssch and irq\n");
	for ( i = 0; i < 16; i ++) {
		len += sprintf ( buf+len, "%4ld ",dasd_io_time2[i] >> shift );
	}
	len += sprintf ( buf+len, "\n");
	len += sprintf ( buf+len, "Histogram of I/O time between ssch and irq per sector\n");
	for ( i = 0; i < 16; i ++) {
		len += sprintf ( buf+len, "%4ld ",dasd_io_time2ps[i] >> shift );
	}
	len += sprintf ( buf+len, "\n");
	len += sprintf ( buf+len, "Histogram of I/O time between irq and end\n");
	for ( i = 0; i < 16; i ++) {
		len += sprintf ( buf+len, "%4ld ",dasd_io_time3[i] >> shift );
	}
	len += sprintf ( buf+len, "\n");
	return len;
}
typedef
struct {
	union {
		unsigned long long  clock;
		struct {
			unsigned int ts1;
			unsigned int ts2 : 20;
			unsigned int unused : 8;
			unsigned int cpu : 4;
		} __attribute__ ((packed)) s;
	} __attribute__ ((packed)) u;
	unsigned long caller_address;
	unsigned long tag;
} __attribute__ ((packed)) dasd_debug_entry;

static dasd_debug_entry *dasd_debug_area = NULL;
static dasd_debug_entry *dasd_debug_actual;
static spinlock_t debug_lock = SPIN_LOCK_UNLOCKED;

void 
dasd_debug ( unsigned long tag )
{
	long flags;
	dasd_debug_entry *d;
	/* initialize in first call ... */
	if ( ! dasd_debug_area ) {
		dasd_debug_actual = dasd_debug_area = 
			(dasd_debug_entry *) get_free_page (GFP_ATOMIC);
		if ( ! dasd_debug_area ) {
			PRINT_WARN("No debug area allocated\n");
			return;
		}
		memset (dasd_debug_area,0,PAGE_SIZE);
	}
	/* renormalize to page */
	spin_lock_irqsave(&debug_lock,flags);
	dasd_debug_actual = (dasd_debug_entry *)
		( (unsigned long) dasd_debug_area +
		  ( ( (unsigned long)dasd_debug_actual -
		      (unsigned long)dasd_debug_area ) % PAGE_SIZE ) );
	d = dasd_debug_actual ++;
	spin_unlock_irqrestore(&debug_lock,flags);
	/* write CPUID to lowest 12 bits of clock... */
	__asm__ __volatile__ ( "STCK  %0"
			       :"=m" (d->u.clock));
	d->tag = tag;
	d -> caller_address = (unsigned long) __builtin_return_address(0);
	d->u.s.cpu = smp_processor_id();
}

int 
dasd_proc_read_debug ( char * buf, char **start, 
		       off_t off, int len, int dd)
{
	dasd_debug_entry *d;
	char tag[9] = { 0, };
	long flags;
	spin_lock_irqsave(&debug_lock,flags);
	len = 0;
       	for( d = dasd_debug_area; 
	     len < 4068 ;
	     d ++ ) {
		if ( *(char*)(&d->tag) == 'D' ) {
			memcpy(tag,&(d->tag),4);
			tag[4]=0;
		}
		else {
			sprintf(tag,"%08lx", d->tag);
			tag[8]=0;
		}
		len += sprintf ( buf+len,
				 "%x %08x%05x %08lx (%8s)\n",
				 d->u.s.cpu, d->u.s.ts1, d->u.s.ts2,
				 d->caller_address,tag);
	}
	spin_unlock_irqrestore(&debug_lock,flags);
	return len;
}

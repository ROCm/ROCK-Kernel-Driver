/*
 *  arch/m68k/q40/config.c
 *
 *  Copyright (C) 1999 Richard Zidlicky
 *
 * originally based on:
 *
 *  linux/bvme/config.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <stdarg.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/serial_reg.h>

#include <asm/rtc.h>
#include <asm/bootinfo.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/q40_master.h>
#include <asm/keyboard.h>

extern void floppy_eject(void);
extern void floppy_setup(char *str, int *ints);

extern void q40_process_int (int level, struct pt_regs *regs);
extern void (*q40_sys_default_handler[]) (int, void *, struct pt_regs *);  /* added just for debugging */
extern void q40_init_IRQ (void);
extern void q40_free_irq (unsigned int, void *);
extern int  q40_get_irq_list (char *);
extern void q40_enable_irq (unsigned int);
extern void q40_disable_irq (unsigned int);
static void q40_get_model(char *model);
static int  q40_get_hardware_list(char *buffer);
extern int  q40_request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *), unsigned long flags, const char *devname, void *dev_id);
extern void q40_sched_init(void (*handler)(int, void *, struct pt_regs *));
extern int  q40_keyb_init(void);
extern int  q40_kbdrate (struct kbd_repeat *);
extern unsigned long q40_gettimeoffset (void);
extern void q40_gettod (int *year, int *mon, int *day, int *hour,
                           int *min, int *sec);
extern int q40_hwclk (int, struct hwclk_time *);
extern int q40_set_clock_mmss (unsigned long);
extern void q40_reset (void);
extern void q40_waitbut(void);
void q40_set_vectors (void);
extern void (*kd_mksound)(unsigned int, unsigned int);
void q40_mksound(unsigned int /*freq*/, unsigned int /*ticks*/ );

extern char *saved_command_line;
extern char m68k_debug_device[];
static void q40_mem_console_write(struct console *co, const char *b,
				    unsigned int count);
#if 0
extern int ql_ticks=0;
extern int sound_ticks=0;
#endif

extern int ql_ticks;

static unsigned char bcd2bin (unsigned char b);
static unsigned char bin2bcd (unsigned char b);

static int q40_wait_key(struct console *co){return 0;}
static struct console q40_console_driver = {
	name:		"debug",
	wait_key:	q40_wait_key,
	flags:		CON_PRINTBUFFER,
	index:		-1,
};


/* early debugging function:*/
extern char *q40_mem_cptr; /*=(char *)0xff020000;*/
static int _cpleft;

int q40_kbd_translate(unsigned char keycode, unsigned char *keycodep, char raw_mode)
{
        *keycodep = keycode;
        return 1;
}

static void q40_mem_console_write(struct console *co, const char *s,
				  unsigned int count)
{
  char *p=(char *)s;

  if (count<_cpleft)
    while (count-- >0){
      *q40_mem_cptr=*p++;
      q40_mem_cptr+=4;
      _cpleft--;
    }
}
#if 0
void printq40(char *str)
{
  int l=strlen(str);
  char *p=q40_mem_cptr;

  while (l-- >0 && _cpleft-- >0)
    {
      *p=*str++;
      p+=4;
    }
  q40_mem_cptr=p;
}
#endif

#if 0
int q40_kbdrate (struct kbd_repeat *k)
{
	return 0;
}
#endif

void q40_reset()
{

        printk ("\n\n*******************************************\n"
		"Called q40_reset : press the RESET button!! \n"
		"*******************************************\n");
	
	while(1) ;
}

static void q40_get_model(char *model)
{
    sprintf(model, "Q40");
}

/* pasted code to make parport_pc happy */
extern __inline__ int __get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}
void *pci_alloc_consistent(void *hwdev, size_t size,
			   dma_addr_t *dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC;

	ret = (void *)__get_free_pages(gfp, __get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
	}
	return ret;
}

void pci_free_consistent(void *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long)vaddr, __get_order(size));
}
/* end pasted code */


/* No hardware options on Q40? */

static int q40_get_hardware_list(char *buffer)
{
    *buffer = '\0';
    return 0;
}

static unsigned int serports[]={0x3f8,0x2f8,0x3e8,0x2e8,0};
void q40_disable_irqs(void)
{
  unsigned i,j;
  
  j=0;
  while((i=serports[j++])) outb(0,i+UART_IER);
  master_outb(0,EXT_ENABLE_REG);
  master_outb(0,KEY_IRQ_ENABLE_REG);
}

void __init config_q40(void)
{
    mach_sched_init      = q40_sched_init;           /* ok */
    /*mach_kbdrate         = q40_kbdrate;*/          /* unneeded ?*/
    mach_keyb_init       = q40_keyb_init;            /* OK */
    mach_kbd_translate   = q40_kbd_translate;
    mach_init_IRQ        = q40_init_IRQ;   
    mach_gettimeoffset   = q40_gettimeoffset; 
    mach_gettod  	 = q40_gettod;
    mach_hwclk           = q40_hwclk; 
    mach_set_clock_mmss	 = q40_set_clock_mmss;
/*  mach_mksound         = q40_mksound; */
    mach_reset		 = q40_reset;           /* use reset button instead !*/
    mach_free_irq	 = q40_free_irq; 
    mach_process_int	 = q40_process_int;
    mach_get_irq_list	 = q40_get_irq_list;
    mach_request_irq	 = q40_request_irq;
    enable_irq		 = q40_enable_irq;
    disable_irq          = q40_disable_irq;
    mach_default_handler = &q40_sys_default_handler;
    mach_get_model       = q40_get_model; /* no use..*/
    mach_get_hardware_list = q40_get_hardware_list; /* no use */
    kd_mksound             = q40_mksound;
    /*mach_kbd_leds        = q40kbd_leds;*/
#ifdef CONFIG_MAGIC_SYSRQ
    mach_sysrq_key       = 0x54;
#endif
    conswitchp = &dummy_con;
#if 0 /*def CONFIG_BLK_DEV_FD*/
    mach_floppy_setup    = floppy_setup;
    mach_floppy_eject    = floppy_eject;
    /**/
#endif

    q40_disable_irqs();
    mach_max_dma_address = 32*1024*1024;   /* no DMA at all, but ide-scsi requires it.. */


/* userfull for early debuging stages writes kernel messages into SRAM */

    if (!strncmp( m68k_debug_device,"mem",3 ))
      {
	/*printk("using NVRAM debug, q40_mem_cptr=%p\n",q40_mem_cptr);*/
	_cpleft=2000-((long)q40_mem_cptr-0xff020000)/4;
	q40_console_driver.write = q40_mem_console_write;
	register_console(&q40_console_driver);
      }
}


int q40_parse_bootinfo(const struct bi_record *rec)
{
  return 1;  /* unknown */
}

#if 0
#define DAC_LEFT  ((unsigned char *)0xff008000)
#define DAC_RIGHT ((unsigned char *)0xff008004)
void q40_mksound(unsigned int hz, unsigned int ticks)
{
  /* for now ignore hz, except that hz==0 switches off sound */
  /* simply alternate the ampl 0-255-0-.. at 200Hz */
  if (hz==0)
    {
      if (sound_ticks)
	sound_ticks=1; /* atomic - no irq spinlock used */

      *DAC_LEFT=0;
      *DAC_RIGHT=0;

      return;
    }
  /* sound itself is done in q40_timer_int */
  if (sound_ticks == 0) sound_ticks=1000; /* pretty long beep */
  sound_ticks=ticks<<1;
}

static void (*q40_timer_routine)(int, void *, struct pt_regs *);
static short rtc_oldsecs=0;
unsigned rtc_irq_flags=0;
unsigned rtc_irq_ctrl=0;

static void q40_timer_int (int irq, void *dev_id, struct pt_regs *fp)
{
#if (HZ==10000)
    master_outb(-1,SAMPLE_CLEAR_REG);
#else /* must be 50 or 100 */
    master_outb(-1,FRAME_CLEAR_REG);
#endif

#if (HZ==100)
    ql_ticks = ql_ticks ? 0 : 1;
    if (sound_ticks)
      {
	unsigned char sval=(sound_ticks & 1) ? 0 : 255;
	sound_ticks--;
	*DAC_LEFT=sval;
	*DAC_RIGHT=sval;
      }
#ifdef CONFIG_Q40RTC
    if (rtc_irq_ctrl && (rtc_oldsecs != RTC_SECS))
      {
	rtc_oldsecs = RTC_SECS;
	rtc_irq_flags = RTC_UIE;
	rtc_interrupt();
      }
#endif
    if (ql_ticks) return;
#endif
    q40_timer_routine(irq, dev_id, fp);
}
#endif

#if 0
extern void (*q40_timer_routine)(int, void *, struct pt_regs *);
extern void q40_timer_int();

void q40_sched_init (void (*timer_routine)(int, void *, struct pt_regs *))
{
    int timer_irq;

    q40_timer_routine = timer_routine;

#if (HZ==10000)
    timer_irq=Q40_IRQ_TIMER;
#else
    timer_irq=Q40_IRQ_FRAME;
#endif

    /*printk("registering sched/timer IRQ %d\n", timer_irq);*/

    if (request_irq(timer_irq, q40_timer_int, 0,
				"timer", q40_timer_int))
	panic ("Couldn't register timer int");

#if (HZ==10000)
    master_outb(SAMPLE_LOW,SAMPLE_RATE_REG);
    master_outb(-1,SAMPLE_CLEAR_REG);
    master_outb(1,SAMPLE_ENABLE_REG);
#else
    master_outb(-1,FRAME_CLEAR_REG);   /* not necessary ? */
#if (HZ==100)
    master_outb( 1,FRAME_RATE_REG);
#endif
#endif
}
#endif

unsigned long q40_gettimeoffset (void)
{
#if (HZ==100)
    return 5000*(ql_ticks!=0);
#else
    return 0;
#endif
}

extern void q40_gettod (int *year, int *mon, int *day, int *hour,
                           int *min, int *sec)
{
	RTC_CTRL |= RTC_READ;
	*year = bcd2bin (RTC_YEAR);
	*mon = bcd2bin (RTC_MNTH)-1;
	*day = bcd2bin (RTC_DATE);
	*hour = bcd2bin (RTC_HOUR);
	*min = bcd2bin (RTC_MINS);
	*sec = bcd2bin (RTC_SECS);
	RTC_CTRL &= ~(RTC_READ);

}

static unsigned char bcd2bin (unsigned char b)
{
	return ((b>>4)*10 + (b&15));
}

static unsigned char bin2bcd (unsigned char b)
{
	return (((b/10)*16) + (b%10));
}


/*
 * Looks like op is non-zero for setting the clock, and zero for
 * reading the clock.
 *
 *  struct hwclk_time {
 *         unsigned        sec;       0..59
 *         unsigned        min;       0..59
 *         unsigned        hour;      0..23
 *         unsigned        day;       1..31
 *         unsigned        mon;       0..11
 *         unsigned        year;      00...
 *         int             wday;      0..6, 0 is Sunday, -1 means unknown/don't set
 * };
 */

int q40_hwclk(int op, struct hwclk_time *t)
{
        if (op)
	{	/* Write.... */
	        RTC_CTRL |= RTC_WRITE;

		RTC_SECS = bin2bcd(t->sec);
		RTC_MINS = bin2bcd(t->min);
		RTC_HOUR = bin2bcd(t->hour);
		RTC_DATE = bin2bcd(t->day);
		RTC_MNTH = bin2bcd(t->mon + 1);
		RTC_YEAR = bin2bcd(t->year%100);
		if (t->wday >= 0)
			RTC_DOW = bin2bcd(t->wday+1);

	        RTC_CTRL &= ~(RTC_WRITE);
	}
	else
	{	/* Read....  */
	  RTC_CTRL |= RTC_READ;

	  t->year = bcd2bin (RTC_YEAR);
	  t->mon  = bcd2bin (RTC_MNTH)-1;
	  t->day  = bcd2bin (RTC_DATE);
	  t->hour = bcd2bin (RTC_HOUR);
	  t->min  = bcd2bin (RTC_MINS);
	  t->sec  = bcd2bin (RTC_SECS);

	  RTC_CTRL &= ~(RTC_READ);
	  
	  if (t->year < 70)
	    t->year += 100;
	  t->wday = bcd2bin(RTC_DOW)-1;

	}

	return 0;
}

/*
 * Set the minutes and seconds from seconds value 'nowtime'.  Fail if
 * clock is out by > 30 minutes.  Logic lifted from atari code.
 * Algorithm is to wait for the 10ms register to change, and then to
 * wait a short while, and then set it.
 */

int q40_set_clock_mmss (unsigned long nowtime)
{
	int retval = 0;
	short real_seconds = nowtime % 60, real_minutes = (nowtime / 60) % 60;

	int rtc_minutes;


	rtc_minutes = bcd2bin (RTC_MINS);

	if ((rtc_minutes < real_minutes
		? real_minutes - rtc_minutes
			: rtc_minutes - real_minutes) < 30)
	{	   
	        RTC_CTRL |= RTC_WRITE;
		RTC_MINS = bin2bcd(real_minutes);
		RTC_SECS = bin2bcd(real_seconds);
		RTC_CTRL &= ~(RTC_WRITE);
	}
	else
		retval = -1;


	return retval;
}

extern void q40kbd_init_hw(void);

int q40_keyb_init (void)
{
        q40kbd_init_hw();
	return 0;
}

#if 0
/* dummy to cause */
void q40_slow_io()
{
  return;
}
#endif

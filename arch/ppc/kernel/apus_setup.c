/*
 *  linux/arch/ppc/kernel/apus_setup.c
 *
 *  Copyright (C) 1998, 1999  Jesper Skov
 *
 *  Basically what is needed to replace functionality found in
 *  arch/m68k allowing Amiga drivers to work under APUS.
 *  Bits of code and/or ideas from arch/m68k and arch/ppc files.
 *
 * TODO:
 *  This file needs a *really* good cleanup. Restructure and optimize.
 *  Make sure it can be compiled for non-APUS configs. Begin to move
 *  Amiga specific stuff into mach/amiga.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kd.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/blk.h>
#include <linux/pci.h>

#ifdef CONFIG_APUS
#include <asm/logging.h>
#endif

/* Needs INITSERIAL call in head.S! */
#undef APUS_DEBUG


#include <linux/ide.h>
#define T_CHAR          (0x0000)        /* char:  don't touch  */
#define T_SHORT         (0x4000)        /* short: 12 -> 21     */
#define T_INT           (0x8000)        /* int:   1234 -> 4321 */
#define T_TEXT          (0xc000)        /* text:  12 -> 21     */

#define T_MASK_TYPE     (0xc000)
#define T_MASK_COUNT    (0x3fff)

#define D_CHAR(cnt)     (T_CHAR  | (cnt))
#define D_SHORT(cnt)    (T_SHORT | (cnt))
#define D_INT(cnt)      (T_INT   | (cnt))
#define D_TEXT(cnt)     (T_TEXT  | (cnt))

static u_short driveid_types[] = {
        D_SHORT(10),    /* config - vendor2 */
        D_TEXT(20),     /* serial_no */
        D_SHORT(3),     /* buf_type, buf_size - ecc_bytes */
        D_TEXT(48),     /* fw_rev - model */
        D_CHAR(2),      /* max_multsect - vendor3 */
        D_SHORT(1),     /* dword_io */
        D_CHAR(2),      /* vendor4 - capability */
        D_SHORT(1),     /* reserved50 */
        D_CHAR(4),      /* vendor5 - tDMA */
        D_SHORT(4),     /* field_valid - cur_sectors */
        D_INT(1),       /* cur_capacity */
        D_CHAR(2),      /* multsect - multsect_valid */
        D_INT(1),       /* lba_capacity */
        D_SHORT(194)    /* dma_1word - reservedyy */
};

#define num_driveid_types       (sizeof(driveid_types)/sizeof(*driveid_types))

#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amigappc.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/init.h>

#include "local_irq.h"

unsigned long m68k_machtype __apusdata;
char debug_device[6] __apusdata = "";

extern void amiga_init_IRQ(void);

void (*mach_sched_init) (void (*handler)(int, void *, struct pt_regs *)) __initdata = NULL;
/* machine dependent keyboard functions */
int (*mach_keyb_init) (void) __initdata = NULL;
int (*mach_kbdrate) (struct kbd_repeat *) __apusdata = NULL;
void (*mach_kbd_leds) (unsigned int) __apusdata = NULL;
/* machine dependent irq functions */
void (*mach_init_IRQ) (void) __initdata = NULL;
void (*(*mach_default_handler)[]) (int, void *, struct pt_regs *) __apusdata = NULL;
void (*mach_get_model) (char *model) __apusdata = NULL;
int (*mach_get_hardware_list) (char *buffer) __apusdata = NULL;
int (*mach_get_irq_list) (char *) __apusdata = NULL;
void (*mach_process_int) (int, struct pt_regs *) __apusdata = NULL;
/* machine dependent timer functions */
unsigned long (*mach_gettimeoffset) (void) __apusdata;
void (*mach_gettod) (int*, int*, int*, int*, int*, int*) __apusdata;
int (*mach_hwclk) (int, struct hwclk_time*) __apusdata = NULL;
int (*mach_set_clock_mmss) (unsigned long) __apusdata = NULL;
void (*mach_reset)( void ) __apusdata;
long mach_max_dma_address __apusdata = 0x00ffffff; /* default set to the lower 16MB */
#if defined(CONFIG_AMIGA_FLOPPY)
void (*mach_floppy_setup) (char *, int *) __initdata = NULL;
void (*mach_floppy_eject) (void) __apusdata = NULL;
#endif
#ifdef CONFIG_HEARTBEAT
void (*mach_heartbeat) (int) __apusdata = NULL;
extern void apus_heartbeat (void);
#endif

extern unsigned long amiga_model;
extern unsigned decrementer_count;/* count value for 1e6/HZ microseconds */
extern unsigned count_period_num; /* 1 decrementer count equals */
extern unsigned count_period_den; /* count_period_num / count_period_den us */

int num_memory __apusdata = 0;
struct mem_info memory[NUM_MEMINFO] __apusdata;/* memory description */
/* FIXME: Duplicate memory data to avoid conflicts with m68k shared code. */
int m68k_realnum_memory __apusdata = 0;
struct mem_info m68k_memory[NUM_MEMINFO] __apusdata;/* memory description */

struct mem_info ramdisk __apusdata;

extern void amiga_floppy_setup(char *, int *);
extern void config_amiga(void);

static int __60nsram __apusdata = 0;

/* for cpuinfo */
static int __bus_speed __apusdata = 0;
static int __speed_test_failed __apusdata = 0;

/********************************************** COMPILE PROTECTION */
/* Provide some stubs that links to Amiga specific functions. 
 * This allows CONFIG_APUS to be removed from generic PPC files while
 * preventing link errors for other PPC targets.
 */
__apus
unsigned long apus_get_rtc_time(void)
{
#ifdef CONFIG_APUS
	extern unsigned long m68k_get_rtc_time(void);
	
	return m68k_get_rtc_time ();
#else
	return 0;
#endif
}

__apus
int apus_set_rtc_time(unsigned long nowtime)
{
#ifdef CONFIG_APUS
	extern int m68k_set_rtc_time(unsigned long nowtime);

	return m68k_set_rtc_time (nowtime);
#else
	return 0;
#endif
}



/* Here some functions we don't support, but which the other ports reference */
int pckbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
	return 0; 
}
int pckbd_getkeycode(unsigned int scancode) 
{ 
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
	return 0; 
}
int pckbd_translate(unsigned char scancode, unsigned char *keycode,
		    char raw_mode) 
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
	return 0; 
}
char pckbd_unexpected_up(unsigned char keycode)
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
	return 0;
}
void pckbd_leds(unsigned char leds)
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
}
void pckbd_init_hw(void)
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
}
unsigned char pckbd_sysrq_xlate[128];

struct pci_bus * __init pci_scan_peer_bridge(int bus)
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
	return NULL;
}

/*********************************************************** SETUP */
/* From arch/m68k/kernel/setup.c. */
void __init apus_setup_arch(void)
{
#ifdef CONFIG_APUS
	extern char cmd_line[];
	int i;
	char *p, *q;

	/* Let m68k-shared code know it should do the Amiga thing. */
	m68k_machtype = MACH_AMIGA;

	/* Parse the command line for arch-specific options.
	 * For the m68k, this is currently only "debug=xxx" to enable printing
	 * certain kernel messages to some machine-specific device.  */
	for( p = cmd_line; p && *p; ) {
	    i = 0;
	    if (!strncmp( p, "debug=", 6 )) {
		    strncpy( debug_device, p+6, sizeof(debug_device)-1 );
		    debug_device[sizeof(debug_device)-1] = 0;
		    if ((q = strchr( debug_device, ' ' ))) *q = 0;
		    i = 1;
	    } else if (!strncmp( p, "60nsram", 7 )) {
		    APUS_WRITE (APUS_REG_WAITSTATE, 
				REGWAITSTATE_SETRESET
				|REGWAITSTATE_PPCR
				|REGWAITSTATE_PPCW);
		    __60nsram = 1;
		    i = 1;
	    }

	    if (i) {
		/* option processed, delete it */
		if ((q = strchr( p, ' ' )))
		    strcpy( p, q+1 );
		else
		    *p = 0;
	    } else {
		if ((p = strchr( p, ' ' ))) ++p;
	    }
	}

	config_amiga();

#if 0 /* Enable for logging - also include logging.o in Makefile rule */
	{
#define LOG_SIZE 4096
		void* base;

		/* Throw away some memory - the P5 firmare stomps on top
		 * of CHIP memory during bootup.
		 */
		amiga_chip_alloc(0x1000);

		base = amiga_chip_alloc(LOG_SIZE+sizeof(klog_data_t));
		LOG_INIT(base, base+sizeof(klog_data_t), LOG_SIZE);
	}
#endif
#endif
}

__apus
int
apus_get_cpuinfo(char *buffer)
{
#ifdef CONFIG_APUS
	extern int __map_without_bats;
	extern unsigned long powerup_PCI_present;
	int len;

	len = sprintf(buffer, "machine\t\t: Amiga\n");
	len += sprintf(buffer+len, "bus speed\t: %d%s", __bus_speed,
		       (__speed_test_failed) ? " [failed]\n" : "\n");
	len += sprintf(buffer+len, "using BATs\t: %s\n",
		       (__map_without_bats) ? "No" : "Yes");
	len += sprintf(buffer+len, "ram speed\t: %dns\n", 
		       (__60nsram) ? 60 : 70);
	len += sprintf(buffer+len, "PCI bridge\t: %s\n",
		       (powerup_PCI_present) ? "Yes" : "No");
	return len;
#endif
}

__apus
static void get_current_tb(unsigned long long *time)
{
	__asm __volatile ("1:mftbu 4      \n\t"
			  "  mftb  5      \n\t"
			  "  mftbu 6      \n\t"
			  "  cmpw  4,6    \n\t"
			  "  bne   1b     \n\t"
			  "  stw   4,0(%0)\n\t"
			  "  stw   5,4(%0)\n\t"
			  : 
			  : "r" (time)
			  : "r4", "r5", "r6");
}


__apus
void apus_calibrate_decr(void)
{
#ifdef CONFIG_APUS
	unsigned long freq;

	/* This algorithm for determining the bus speed was
           contributed by Ralph Schmidt. */
	unsigned long long start, stop;
	int bus_speed;
	int speed_test_failed = 0;

	{
		unsigned long loop = amiga_eclock / 10;

		get_current_tb (&start);
		while (loop--) {
			unsigned char tmp;

			tmp = ciaa.pra;
		}
		get_current_tb (&stop);
	}

	bus_speed = (((unsigned long)(stop-start))*10*4) / 1000000;
	if (AMI_1200 == amiga_model)
		bus_speed /= 2;

	if ((bus_speed >= 47) && (bus_speed < 53)) {
		bus_speed = 50;
		freq = 12500000;
	} else if ((bus_speed >= 57) && (bus_speed < 63)) {
		bus_speed = 60;
		freq = 15000000;
	} else if ((bus_speed >= 63) && (bus_speed < 69)) {
		bus_speed = 67;
		freq = 16666667;
	} else {
		printk ("APUS: Unable to determine bus speed (%d). "
			"Defaulting to 50MHz", bus_speed);
		bus_speed = 50;
		freq = 12500000;
		speed_test_failed = 1;
	}

	/* Ease diagnostics... */
	{
		extern int __map_without_bats;
		extern unsigned long powerup_PCI_present;

		printk ("APUS: BATs=%d, BUS=%dMHz",
			(__map_without_bats) ? 0 : 1,
			bus_speed);
		if (speed_test_failed)
			printk ("[FAILED - please report]");

		printk (", RAM=%dns, PCI bridge=%d\n",
			(__60nsram) ? 60 : 70,
			(powerup_PCI_present) ? 1 : 0);

		/* print a bit more if asked politely... */
		if (!(ciaa.pra & 0x40)){
			extern unsigned int bat_addrs[4][3];
			int b;
			for (b = 0; b < 4; ++b) {
				printk ("APUS: BAT%d ", b);
				printk ("%08x-%08x -> %08x\n",
					bat_addrs[b][0],
					bat_addrs[b][1],
					bat_addrs[b][2]);
			}
		}

	}

        printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       freq/1000000, freq%1000000);
	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	__bus_speed = bus_speed;
	__speed_test_failed = speed_test_failed;
#endif
}

__apus
void arch_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
#ifdef CONFIG_APUS
	if (mach_gettod)
		mach_gettod(year, mon, day, hour, min, sec);
	else
		*year = *mon = *day = *hour = *min = *sec = 0;
#endif
}

/* for "kbd-reset" cmdline param */
__init
void kbd_reset_setup(char *str, int *ints)
{
}

/*********************************************************** FLOPPY */
#if defined(CONFIG_AMIGA_FLOPPY)
__init 
void floppy_setup(char *str, int *ints)
{
	if (mach_floppy_setup)
		mach_floppy_setup (str, ints);
}

__apus
void floppy_eject(void)
{
	if (mach_floppy_eject)
		mach_floppy_eject();
}
#endif

/*********************************************************** MEMORY */
#define KMAP_MAX 32
unsigned long kmap_chunks[KMAP_MAX*3] __apusdata;
int kmap_chunk_count __apusdata = 0;

/* From pgtable.h */
__apus
static __inline__ pte_t *my_find_pte(struct mm_struct *mm,unsigned long va)
{
	pgd_t *dir = 0;
	pmd_t *pmd = 0;
	pte_t *pte = 0;

	va &= PAGE_MASK;
	
	dir = pgd_offset( mm, va );
	if (dir)
	{
		pmd = pmd_offset(dir, va & PAGE_MASK);
		if (pmd && pmd_present(*pmd))
		{
			pte = pte_offset(pmd, va);
		}
	}
	return pte;
}


/* Again simulating an m68k/mm/kmap.c function. */
__apus
void kernel_set_cachemode( unsigned long address, unsigned long size,
			   unsigned int cmode )
{
	unsigned long mask, flags;

	switch (cmode)
	{
	case IOMAP_FULL_CACHING:
		mask = ~(_PAGE_NO_CACHE | _PAGE_GUARDED);
		flags = 0;
		break;
	case IOMAP_NOCACHE_SER:
		mask = ~0;
		flags = (_PAGE_NO_CACHE | _PAGE_GUARDED);
		break;
	default:
		panic ("kernel_set_cachemode() doesn't support mode %d\n", 
		       cmode);
		break;
	}
	
	size /= PAGE_SIZE;
	address &= PAGE_MASK;
	while (size--)
	{
		pte_t *pte;

		pte = my_find_pte(&init_mm, address);
		if ( !pte )
		{
			printk("pte NULL in kernel_set_cachemode()\n");
			return;
		}

                pte_val (*pte) &= mask;
                pte_val (*pte) |= flags;
                flush_tlb_page(find_vma(&init_mm,address),address);

		address += PAGE_SIZE;
	}
}

__apus
unsigned long mm_ptov (unsigned long paddr)
{
	unsigned long ret;
	if (paddr < 16*1024*1024)
		ret = ZTWO_VADDR(paddr);
	else {
		int i;

		for (i = 0; i < kmap_chunk_count;){
			unsigned long phys = kmap_chunks[i++];
			unsigned long size = kmap_chunks[i++];
			unsigned long virt = kmap_chunks[i++];
			if (paddr >= phys
			    && paddr < (phys + size)){
				ret = virt + paddr - phys;
				goto exit;
			}
		}
		
		ret = (unsigned long) __va(paddr);
	}
exit:
#ifdef DEBUGPV
	printk ("PTOV(%lx)=%lx\n", paddr, ret);
#endif
	return ret;
}

__apus
int mm_end_of_chunk (unsigned long addr, int len)
{
	if (memory[0].addr + memory[0].size == addr + len)
		return 1;
	return 0;
}

/*********************************************************** CACHE */

#define L1_CACHE_BYTES 32
#define MAX_CACHE_SIZE 8192
__apus
void cache_push(__u32 addr, int length)
{
	addr = mm_ptov(addr);

	if (MAX_CACHE_SIZE < length)
		length = MAX_CACHE_SIZE;

	while(length > 0){
		__asm ("dcbf 0,%0\n\t"
		       : : "r" (addr));
		addr += L1_CACHE_BYTES;
		length -= L1_CACHE_BYTES;
	}
	/* Also flush trailing block */
	__asm ("dcbf 0,%0\n\t"
	       "sync \n\t"
	       : : "r" (addr));
}

__apus
void cache_clear(__u32 addr, int length)
{
	if (MAX_CACHE_SIZE < length)
		length = MAX_CACHE_SIZE;

	addr = mm_ptov(addr);

	__asm ("dcbf 0,%0\n\t"
	       "sync \n\t"
	       "icbi 0,%0 \n\t"
	       "isync \n\t"
	       : : "r" (addr));
	
	addr += L1_CACHE_BYTES;
	length -= L1_CACHE_BYTES;

	while(length > 0){
		__asm ("dcbf 0,%0\n\t"
		       "sync \n\t"
		       "icbi 0,%0 \n\t"
		       "isync \n\t"
		       : : "r" (addr));
		addr += L1_CACHE_BYTES;
		length -= L1_CACHE_BYTES;
	}

	__asm ("dcbf 0,%0\n\t"
	       "sync \n\t"
	       "icbi 0,%0 \n\t"
	       "isync \n\t"
	       : : "r" (addr));
}

/****************************************************** from setup.c */
void
apus_restart(char *cmd)
{
	cli();

	APUS_WRITE(APUS_REG_LOCK, 
		   REGLOCK_BLACKMAGICK1|REGLOCK_BLACKMAGICK2);
	APUS_WRITE(APUS_REG_LOCK, 
		   REGLOCK_BLACKMAGICK1|REGLOCK_BLACKMAGICK3);
	APUS_WRITE(APUS_REG_LOCK, 
		   REGLOCK_BLACKMAGICK2|REGLOCK_BLACKMAGICK3);
	APUS_WRITE(APUS_REG_SHADOW, REGSHADOW_SELFRESET);
	APUS_WRITE(APUS_REG_RESET, REGRESET_AMIGARESET);
	for(;;);
}

void
apus_power_off(void)
{
	for (;;);
}

void
apus_halt(void)
{
   apus_restart(NULL);
}

/****************************************************** from setup.c/IDE */
#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE stuff.
 */
void ide_insw(ide_ioreg_t port, void *buf, int ns);
void ide_outsw(ide_ioreg_t port, void *buf, int ns);
void
apus_ide_insw(ide_ioreg_t port, void *buf, int ns)
{
	ide_insw(port, buf, ns);
}

void
apus_ide_outsw(ide_ioreg_t port, void *buf, int ns)
{
	ide_outsw(port, buf, ns);
}

int
apus_ide_default_irq(ide_ioreg_t base)
{
        return 0;
}

ide_ioreg_t
apus_ide_default_io_base(int index)
{
        return 0;
}

int
apus_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
        return 0;
}

void
apus_ide_request_region(ide_ioreg_t from,
			unsigned int extent,
			const char *name)
{
}

void
apus_ide_release_region(ide_ioreg_t from,
			unsigned int extent)
{
}

void
apus_ide_fix_driveid(struct hd_driveid *id)
{
   u_char *p = (u_char *)id;
   int i, j, cnt;
   u_char t;

   if (!MACH_IS_AMIGA && !MACH_IS_MAC)
   	return;
   for (i = 0; i < num_driveid_types; i++) {
      cnt = driveid_types[i] & T_MASK_COUNT;
      switch (driveid_types[i] & T_MASK_TYPE) {
         case T_CHAR:
            p += cnt;
            break;
         case T_SHORT:
            for (j = 0; j < cnt; j++) {
               t = p[0];
               p[0] = p[1];
               p[1] = t;
               p += 2;
            }
            break;
         case T_INT:
            for (j = 0; j < cnt; j++) {
               t = p[0];
               p[0] = p[3];
               p[3] = t;
               t = p[1];
               p[1] = p[2];
               p[2] = t;
               p += 4;
            }
            break;
         case T_TEXT:
            for (j = 0; j < cnt; j += 2) {
               t = p[0];
               p[0] = p[1];
               p[1] = t;
               p += 2;
            }
            break;
      }
   }
}

__init
void apus_ide_init_hwif_ports (hw_regs_t *hw, ide_ioreg_t data_port, 
			       ide_ioreg_t ctrl_port, int *irq)
{
        if (data_port || ctrl_port)
                printk("apus_ide_init_hwif_ports: must not be called\n");
}
#endif
/****************************************************** IRQ stuff */

__apus
static unsigned int apus_irq_cannonicalize(unsigned int irq)
{
	return irq;
}

__apus
int apus_get_irq_list(char *buf)
{
#ifdef CONFIG_APUS
	extern int amiga_get_irq_list(char *buf);
	
	return amiga_get_irq_list (buf);
#else
	return 0;
#endif
}

/* IPL must be between 0 and 7 */
__apus
static inline void apus_set_IPL(unsigned long ipl)
{
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET | IPLEMU_DISABLEINT);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_IPLMASK);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET | ((~ipl) & IPLEMU_IPLMASK));
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_DISABLEINT);
}

__apus
static inline unsigned long apus_get_IPL(void)
{
	/* This returns the present IPL emulation level. */
	unsigned long __f;
	APUS_READ(APUS_IPL_EMU, __f);
	return ((~__f) & IPLEMU_IPLMASK);
}

__apus
static inline unsigned long apus_get_prev_IPL(struct pt_regs* regs)
{
	/* The value saved in mq is the IPL_EMU value at the time of
	   interrupt. The lower bits are the current interrupt level,
	   the upper bits the requested level. Thus, to restore the
	   IPL level to the post-interrupt state, we will need to use
	   the lower bits. */
	unsigned long __f = regs->mq;
	return ((~__f) & IPLEMU_IPLMASK);
}


#ifdef CONFIG_APUS
void free_irq(unsigned int irq, void *dev_id)
{
	extern void amiga_free_irq(unsigned int irq, void *dev_id);

	amiga_free_irq (irq, dev_id);
}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags, const char * devname, void *dev_id)
{
	extern int  amiga_request_irq(unsigned int irq, 
				      void (*handler)(int, void *, 
						      struct pt_regs *),
				      unsigned long flags, 
				      const char *devname, 
				      void *dev_id);

	return amiga_request_irq (irq, handler, irqflags, devname, dev_id);
}

/* In Linux/m68k the sys_request_irq deals with vectors 0-7. That's what
   callers expect - but on Linux/APUS we actually use the IRQ_AMIGA_AUTO
   vectors (24-31), so we put this dummy function in between to adjust
   the vector argument (rather have cruft here than in the generic irq.c). */
int sys_request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		    unsigned long irqflags, const char * devname, void *dev_id)
{
	extern int request_sysirq(unsigned int irq, 
				  void (*handler)(int, void *, 
						  struct pt_regs *),
				  unsigned long irqflags,
				  const char * devname, void *dev_id);
	return request_sysirq(irq+IRQ_AMIGA_AUTO, handler, irqflags, 
			      devname, dev_id);
}
#endif

__apus
int apus_get_irq(struct pt_regs* regs)
{
#ifdef CONFIG_APUS
	int level = apus_get_IPL();

#ifdef __INTERRUPT_DEBUG
	printk("<%d:%d>", level, apus_get_prev_IPL(regs));
#endif

	if (0 == level)
		return -8;
	if (7 == level)
		return -9;

	return level + IRQ_AMIGA_AUTO;
#else
	return 0;
#endif
}


__apus
void apus_post_irq(struct pt_regs* regs, int level)
{
#ifdef __INTERRUPT_DEBUG
	printk("{%d}", apus_get_prev_IPL(regs));
#endif
	/* Restore IPL to the previous value */
	apus_set_IPL(apus_get_prev_IPL(regs));
}



/****************************************************** keyboard */
__apus
static int apus_kbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	return -EOPNOTSUPP;
}

__apus
static int apus_kbd_getkeycode(unsigned int scancode)
{
	return scancode > 127 ? -EINVAL : scancode;
}

__apus
static int apus_kbd_translate(unsigned char keycode, unsigned char *keycodep,
			      char raw_mode)
{
	*keycodep = keycode;
	return 1;
}

__apus
static char apus_kbd_unexpected_up(unsigned char keycode)
{
	return 0200;
}

__apus  
static void apus_kbd_leds(unsigned char leds)
{
}

__apus  
static void apus_kbd_init_hw(void)
{
#ifdef CONFIG_APUS
	extern int amiga_keyb_init(void);

	amiga_keyb_init();
#endif
}


/****************************************************** debugging */

/* some serial hardware definitions */
#define SDR_OVRUN   (1<<15)
#define SDR_RBF     (1<<14)
#define SDR_TBE     (1<<13)
#define SDR_TSRE    (1<<12)

#define AC_SETCLR   (1<<15)
#define AC_UARTBRK  (1<<11)

#define SER_DTR     (1<<7)
#define SER_RTS     (1<<6)
#define SER_DCD     (1<<5)
#define SER_CTS     (1<<4)
#define SER_DSR     (1<<3)

static __inline__ void ser_RTSon(void)
{
    ciab.pra &= ~SER_RTS; /* active low */
}

__apus
int __debug_ser_out( unsigned char c )
{
	custom.serdat = c | 0x100;
	mb();
	while (!(custom.serdatr & 0x2000))
		barrier();
	return 1;
}

__apus
unsigned char __debug_ser_in( void )
{
	unsigned char c;

	/* XXX: is that ok?? derived from amiga_ser.c... */
	while( !(custom.intreqr & IF_RBF) )
		barrier();
	c = custom.serdatr;
	/* clear the interrupt, so that another character can be read */
	custom.intreq = IF_RBF;
	return c;
}

__apus
int __debug_serinit( void )
{	
	unsigned long flags;
	
	save_flags (flags);
	cli();

	/* turn off Rx and Tx interrupts */
	custom.intena = IF_RBF | IF_TBE;

	/* clear any pending interrupt */
	custom.intreq = IF_RBF | IF_TBE;

	restore_flags (flags);

	/*
	 * set the appropriate directions for the modem control flags,
	 * and clear RTS and DTR
	 */
	ciab.ddra |= (SER_DTR | SER_RTS);   /* outputs */
	ciab.ddra &= ~(SER_DCD | SER_CTS | SER_DSR);  /* inputs */
	
#ifdef CONFIG_KGDB
	/* turn Rx interrupts on for GDB */
	custom.intena = IF_SETCLR | IF_RBF;
	ser_RTSon();
#endif

	return 0;
}

__apus
void __debug_print_hex(unsigned long x)
{
	int i;
	char hexchars[] = "0123456789ABCDEF";

	for (i = 0; i < 8; i++) {
		__debug_ser_out(hexchars[(x >> 28) & 15]);
		x <<= 4;
	}
	__debug_ser_out('\n');
	__debug_ser_out('\r');
}

__apus
void __debug_print_string(char* s)
{
	unsigned char c;
	while((c = *s++))
		__debug_ser_out(c);
	__debug_ser_out('\n');
	__debug_ser_out('\r');
}

__apus
static void apus_progress(char *s, unsigned short value)
{
	__debug_print_string(s);
}

/****************************************************** init */

/* The number of spurious interrupts */
volatile unsigned int num_spurious;

#define NUM_IRQ_NODES 100
static irq_node_t nodes[NUM_IRQ_NODES];

extern void (*amiga_default_handler[AUTO_IRQS])(int, void *, struct pt_regs *);

static const char *default_names[SYS_IRQS] = {
	"spurious int", "int1 handler", "int2 handler", "int3 handler",
	"int4 handler", "int5 handler", "int6 handler", "int7 handler"
};

irq_node_t *new_irq_node(void)
{
	irq_node_t *node;
	short i;

	for (node = nodes, i = NUM_IRQ_NODES-1; i >= 0; node++, i--)
		if (!node->handler)
			return node;

	printk ("new_irq_node: out of nodes\n");
	return NULL;
}

extern void amiga_enable_irq(unsigned int irq);
extern void amiga_disable_irq(unsigned int irq);

struct hw_interrupt_type amiga_irqctrl = {
	" Amiga  ",
	NULL,
	NULL,
	amiga_enable_irq,
	amiga_disable_irq,
	0,
	0
};


__init
void apus_init_IRQ(void)
{
	int i;

	for ( i = 0 ; i < NR_IRQS ; i++ )
		irq_desc[i].handler = &amiga_irqctrl;

	for (i = 0; i < NUM_IRQ_NODES; i++)
		nodes[i].handler = NULL;

	for (i = 0; i < AUTO_IRQS; i++) {
		if (amiga_default_handler[i] != NULL)
			sys_request_irq(i, amiga_default_handler[i],
					0, default_names[i], NULL);
	}

	amiga_init_IRQ();

	int_control.int_sti = __no_use_sti;
	int_control.int_cli = __no_use_cli;
	int_control.int_save_flags = __no_use_save_flags;
	int_control.int_restore_flags = __no_use_restore_flags;
}

__init
void apus_init(unsigned long r3, unsigned long r4, unsigned long r5,
	       unsigned long r6, unsigned long r7)
{
	extern int parse_bootinfo(const struct bi_record *);
	extern char _end[];
	
	/* Parse bootinfo. The bootinfo is located right after
           the kernel bss */
	parse_bootinfo((const struct bi_record *)&_end);
#ifdef CONFIG_BLK_DEV_INITRD
	/* Take care of initrd if we have one. Use data from
	   bootinfo to avoid the need to initialize PPC
	   registers when kernel is booted via a PPC reset. */
	if ( ramdisk.addr ) {
		initrd_start = (unsigned long) __va(ramdisk.addr);
		initrd_end = (unsigned long) 
			__va(ramdisk.size + ramdisk.addr);
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	ISA_DMA_THRESHOLD = 0x00ffffff;

	ppc_md.setup_arch     = apus_setup_arch;
	ppc_md.setup_residual = NULL;
	ppc_md.get_cpuinfo    = apus_get_cpuinfo;
	ppc_md.irq_cannonicalize = apus_irq_cannonicalize;
	ppc_md.init_IRQ       = apus_init_IRQ;
	ppc_md.get_irq        = apus_get_irq;
	ppc_md.post_irq       = apus_post_irq;
#ifdef CONFIG_HEARTBEAT
	ppc_md.heartbeat      = apus_heartbeat;
	ppc_md.heartbeat_count = 1;
#endif
#ifdef APUS_DEBUG
	__debug_serinit();
	ppc_md.progress       = apus_progress;
#endif
	ppc_md.init           = NULL;

	ppc_md.restart        = apus_restart;
	ppc_md.power_off      = apus_power_off;
	ppc_md.halt           = apus_halt;

	ppc_md.time_init      = NULL;
	ppc_md.set_rtc_time   = apus_set_rtc_time;
	ppc_md.get_rtc_time   = apus_get_rtc_time;
	ppc_md.calibrate_decr = apus_calibrate_decr;

	ppc_md.nvram_read_val = NULL;
	ppc_md.nvram_write_val = NULL;

	/* These should not be used for the APUS yet, since it uses
	   the M68K keyboard now. */
	ppc_md.kbd_setkeycode    = apus_kbd_setkeycode;
	ppc_md.kbd_getkeycode    = apus_kbd_getkeycode;
	ppc_md.kbd_translate     = apus_kbd_translate;
	ppc_md.kbd_unexpected_up = apus_kbd_unexpected_up;
	ppc_md.kbd_leds          = apus_kbd_leds;
	ppc_md.kbd_init_hw       = apus_kbd_init_hw;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.kbd_sysrq_xlate	 = NULL;
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
        ppc_ide_md.insw = apus_ide_insw;
        ppc_ide_md.outsw = apus_ide_outsw;
        ppc_ide_md.default_irq = apus_ide_default_irq;
        ppc_ide_md.default_io_base = apus_ide_default_io_base;
        ppc_ide_md.ide_check_region = apus_ide_check_region;
        ppc_ide_md.ide_request_region = apus_ide_request_region;
        ppc_ide_md.ide_release_region = apus_ide_release_region;
        ppc_ide_md.fix_driveid = apus_ide_fix_driveid;
        ppc_ide_md.ide_init_hwif = apus_ide_init_hwif_ports;

        ppc_ide_md.io_base = _IO_BASE;
#endif		
}


/*************************************************** coexistence */
void __init adbdev_init(void)
{
}

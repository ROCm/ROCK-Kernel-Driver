/*
 * BK Id: SCCS/s.misc.c 1.14 06/16/01 20:43:20 trini
 */
/*
 * misc.c
 *
 * Adapted for PowerPC by Gary Thomas
 *
 * Rewritten by Cort Dougan (cort@cs.nmt.edu)
 * One day to be replaced by a single bootloader for chrp/prep/pmac. -- Cort
 */

#include <linux/types.h>
#include "zlib.h"
#include <asm/residual.h>
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/elf.h>
#include <linux/pci_ids.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/bootinfo.h>
#include <asm/mmu.h>
#include <asm/byteorder.h>
#if defined(CONFIG_SERIAL_CONSOLE)
unsigned long com_port;
#endif /* CONFIG_SERIAL_CONSOLE */

/*
 * Please send me load/board info and such data for hardware not
 * listed here so I can keep track since things are getting tricky
 * with the different load addrs with different firmware.  This will
 * help to avoid breaking the load/boot process.
 * -- Cort
 */
char *avail_ram;
char *end_avail;
extern char _end[];

#ifdef CONFIG_CMDLINE
#define CMDLINE CONFIG_CMDLINE
#else
#define CMDLINE "";
#endif
char cmd_preset[] = CMDLINE;
char cmd_buf[256];
char *cmd_line = cmd_buf;

int keyb_present = 1;	/* keyboard controller is present by default */
RESIDUAL hold_resid_buf;
RESIDUAL *hold_residual = &hold_resid_buf;
unsigned long initrd_start = 0, initrd_end = 0;
char *zimage_start;
int zimage_size;

char *vidmem = (char *)0xC00B8000;
int lines, cols;
int orig_x, orig_y;

extern void puts(const char *);
extern void putc(const char c);
extern int tstc(void);
extern int getc(void);
extern void puthex(unsigned long val);
extern void * memcpy(void * __dest, __const void * __src, __kernel_size_t __n);
extern int CRT_tstc(void);
extern void of_init(void *handler);
extern int of_finddevice(const char *device_specifier, int *phandle);
extern int of_getprop(int phandle, const char *name, void *buf, int buflen, 
		int *size);
extern __kernel_size_t strlen(const char *s);
extern int vga_init(unsigned char *ISA_mem);
extern void udelay(long x);
void gunzip(void *, int, unsigned char *, int *);
unsigned char inb(int);

void
writel(unsigned int val, unsigned int address)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	*(unsigned int *)address = cpu_to_le32(val);
}

unsigned int 
readl(unsigned int address)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	return le32_to_cpu(*(unsigned int *)address);
}

#define PCI_CFG_ADDR(dev,off)	((0x80<<24) | (dev<<8) | (off&0xfc)) 
#define PCI_CFG_DATA(off)	(0x80000cfc+(off&3))  

static void
pci_read_config_32(unsigned char devfn,
		unsigned char offset,
		unsigned int *val)
{
	writel(PCI_CFG_ADDR(devfn,offset), 0x80000cf8);
	*val = readl(PCI_CFG_DATA(offset));
	return;
}

#ifdef CONFIG_VGA_CONSOLE
void
scroll()
{
	int i;

	memcpy ( vidmem, vidmem + cols * 2, ( lines - 1 ) * cols * 2 );
	for ( i = ( lines - 1 ) * cols * 2; i < lines * cols * 2; i += 2 )
		vidmem[i] = ' ';
}
#endif /* CONFIG_VGA_CONSOLE */

/*
 * This routine is used to control the second processor on the 
 * Motorola dual processor platforms.  
 */
void
park_cpus()
{
	volatile void (*go)(RESIDUAL *, int, int, char *, int);
	unsigned int i;
	volatile unsigned long *smp_iar = &(hold_residual->VitalProductData.SmpIar);

	/* Wait for indication to continue.  If the kernel
	   was not compiled with SMP support then the second 
	   processor will spin forever here makeing the kernel
	   multiprocessor safe. */
	while (*smp_iar == 0) {
                for (i=0; i < 512; i++);
	}

	(unsigned long)go = hold_residual->VitalProductData.SmpIar;
	go(hold_residual, 0, 0, cmd_line, sizeof(cmd_preset));
}

unsigned long
decompress_kernel(unsigned long load_addr, int num_words, unsigned long cksum,
		  RESIDUAL *residual, void *OFW_interface)
{
	int timer;
	extern unsigned long start;
	char *cp, ch;
	unsigned long TotalMemory;
	unsigned long orig_MSR;
	int dev_handle;
	int mem_info[2];
	int res, size;
	unsigned char board_type;
	unsigned char base_mod;
	int start_multi = 0;
	unsigned int pci_viddid, pci_did, tulip_pci_base, tulip_base;

	lines = 25;
	cols = 80;
	orig_x = 0;
	orig_y = 24;
	
	/*
	 * IBM's have the MMU on, so we have to disable it or
	 * things get really unhappy in the kernel when
	 * trying to setup the BATs with the MMU on
	 * -- Cort
	 */
	flush_instruction_cache();
	_put_HID0(_get_HID0() & ~0x0000C000);
	_put_MSR((orig_MSR = _get_MSR()) & ~0x0030);

#if defined(CONFIG_SERIAL_CONSOLE)
	com_port = serial_init(0);
#endif /* CONFIG_SERIAL_CONSOLE */
#if defined(CONFIG_VGA_CONSOLE)
	vga_init((unsigned char *)0xC0000000);
#endif /* CONFIG_VGA_CONSOLE */

	if (residual)
	{
		/* Is this Motorola PPCBug? */
		if ((1 & residual->VitalProductData.FirmwareSupports) &&
		    (1 == residual->VitalProductData.FirmwareSupplier)) {
			board_type = inb(0x800) & 0xF0;

			/*
			 * Reset the onboard 21x4x Ethernet
			 * Motorola Ethernet is at IDSEL 14 (devfn 0x70)
			 */
			pci_read_config_32(0x70, 0x00, &pci_viddid);
			pci_did = (pci_viddid & 0xffff0000) >> 16;
			/* Be sure we've really found a 21x4x chip */
			if (((pci_viddid & 0xffff) == PCI_VENDOR_ID_DEC) &&
				((pci_did == PCI_DEVICE_ID_DEC_TULIP_FAST) ||
				(pci_did == PCI_DEVICE_ID_DEC_TULIP) ||
				(pci_did == PCI_DEVICE_ID_DEC_TULIP_PLUS) ||
				(pci_did == PCI_DEVICE_ID_DEC_21142)))
			{
				pci_read_config_32(0x70,
						0x10,
						&tulip_pci_base);
				/* Get the physical base address */
				tulip_base =
					(tulip_pci_base & ~0x03UL) + 0x80000000;
				/* Strobe the 21x4x reset bit in CSR0 */
				writel(0x1, tulip_base);
			}

			/* If this is genesis 2 board then check for no
			 * keyboard controller and more than one processor.
			 */
			if (board_type == 0xe0) {	
				base_mod = inb(0x803);
				/* if a MVME2300/2400 or a Sitka then no keyboard */
				if((base_mod == 0xFA) || (base_mod == 0xF9) ||
				   (base_mod == 0xE1)) {
					keyb_present = 0;	/* no keyboard */
				}
			}
			/* If this is a multiprocessor system then
			 * park the other processor so that the
			 * kernel knows where to find them.
			 */
			if (residual->MaxNumCpus > 1) {
				start_multi = 1;
			}
		}
		memcpy(hold_residual,residual,sizeof(RESIDUAL));
	} else {
		/* Assume 32M in the absence of more info... */
		TotalMemory = 0x02000000;
		/*
		 * This is a 'best guess' check.  We want to make sure
		 * we don't try this on a PReP box without OF
		 *     -- Cort
		 */
		while (OFW_interface && ((unsigned long)OFW_interface < 0x10000000) )
		{
			/* The MMU needs to be on when we call OFW */
			_put_MSR(orig_MSR);
			of_init(OFW_interface);

			/* get handle to memory description */
			res = of_finddevice("/memory@0", 
					    &dev_handle);
			// puthex(res);  puts("\n");
			if (res) break;
			
			/* get the info */
			// puts("get info = ");
			res = of_getprop(dev_handle, 
					 "reg", 
					 mem_info, 
					 sizeof(mem_info), 
					 &size);
			// puthex(res);  puts(", info = "); puthex(mem_info[0]);  
			// puts(" ");  puthex(mem_info[1]);   puts("\n");
			if (res) break;
			
			TotalMemory = mem_info[1];
			break;
		}
		hold_residual->TotalMemory = TotalMemory;
		residual = hold_residual;
		/* Turn MMU back off */
		_put_MSR(orig_MSR & ~0x0030);
        }

	if (start_multi) {
		hold_residual->VitalProductData.SmpIar = 0;
		hold_residual->Cpus[1].CpuState = CPU_GOOD_FW;
		residual->VitalProductData.SmpIar = (unsigned long)park_cpus;
		residual->Cpus[1].CpuState = CPU_GOOD;
		hold_residual->VitalProductData.Reserved5 = 0xdeadbeef;
	}

	/* assume the chunk below 8M is free */
	end_avail = (char *)0x00800000;

	/* tell the user where we were loaded at and where we
	 * were relocated to for debugging this process
	 */
	puts("loaded at:     "); puthex(load_addr);
	puts(" "); puthex((unsigned long)(load_addr + (4*num_words))); puts("\n");
	if ( (unsigned long)load_addr != (unsigned long)&start )
	{
		puts("relocated to:  "); puthex((unsigned long)&start);
		puts(" ");
		puthex((unsigned long)((unsigned long)&start + (4*num_words)));
		puts("\n");
	}

	if ( residual )
	{
		puts("board data at: "); puthex((unsigned long)residual);
		puts(" ");
		puthex((unsigned long)((unsigned long)residual + sizeof(RESIDUAL)));
		puts("\n");
		puts("relocated to:  ");
		puthex((unsigned long)hold_residual);
		puts(" ");
		puthex((unsigned long)((unsigned long)hold_residual + sizeof(RESIDUAL)));
		puts("\n");
	}

	/* we have to subtract 0x10000 here to correct for objdump including the
	   size of the elf header which we strip -- Cort */
	zimage_start = (char *)(load_addr - 0x10000 + ZIMAGE_OFFSET);
	zimage_size = ZIMAGE_SIZE;

	if ( INITRD_OFFSET )
		initrd_start = load_addr - 0x10000 + INITRD_OFFSET;
	else
		initrd_start = 0;
	initrd_end = INITRD_SIZE + initrd_start;

	/*
	 * Find a place to stick the zimage and initrd and 
	 * relocate them if we have to. -- Cort
	 */
	avail_ram = (char *)PAGE_ALIGN((unsigned long)_end);
	puts("zimage at:     "); puthex((unsigned long)zimage_start);
	puts(" "); puthex((unsigned long)(zimage_size+zimage_start)); puts("\n");
	if ( (unsigned long)zimage_start <= 0x00800000 )
	{
		memcpy( (void *)avail_ram, (void *)zimage_start, zimage_size );
		zimage_start = (char *)avail_ram;
		puts("relocated to:  "); puthex((unsigned long)zimage_start);
		puts(" ");
		puthex((unsigned long)zimage_size+(unsigned long)zimage_start);
		puts("\n");

		/* relocate initrd */
		if ( initrd_start )
		{
			puts("initrd at:     "); puthex(initrd_start);
			puts(" "); puthex(initrd_end); puts("\n");
			avail_ram = (char *)PAGE_ALIGN(
				(unsigned long)zimage_size+(unsigned long)zimage_start);
			memcpy ((void *)avail_ram, (void *)initrd_start, INITRD_SIZE );
			initrd_start = (unsigned long)avail_ram;
			initrd_end = initrd_start + INITRD_SIZE;
			puts("relocated to:  "); puthex(initrd_start);
			puts(" "); puthex(initrd_end); puts("\n");
		}
	} else if ( initrd_start ) {
		puts("initrd at:     "); puthex(initrd_start);
		puts(" "); puthex(initrd_end); puts("\n");
	}

	avail_ram = (char *)0x00400000;
	end_avail = (char *)0x00800000;
	puts("avail ram:     "); puthex((unsigned long)avail_ram); puts(" ");
	puthex((unsigned long)end_avail); puts("\n");

	if (keyb_present)
		CRT_tstc();  /* Forces keyboard to be initialized */

	puts("\nLinux/PPC load: ");
	timer = 0;
	cp = cmd_line;
	memcpy (cmd_line, cmd_preset, sizeof(cmd_preset));
	while ( *cp ) putc(*cp++);
	while (timer++ < 5*1000) {
		if (tstc()) {
			while ((ch = getc()) != '\n' && ch != '\r') {
				/* Test for backspace/delete */
				if (ch == '\b' || ch == '\177') {
					if (cp != cmd_line) {
						cp--;
						puts("\b \b");
					}
				/* Test for ^x/^u (and wipe the line) */
				} else if (ch == '\030' || ch == '\025') {
					while (cp != cmd_line) {
						cp--;
						puts("\b \b");
					}
				} else {
					*cp++ = ch;
					putc(ch);
				}
			}
			break;  /* Exit 'timer' loop */
		}
		udelay(1000);  /* 1 msec */
	}
	*cp = 0;
	puts("\n");

	/* mappings on early boot can only handle 16M */
	if ( (int)(cmd_line[0]) > (16<<20))
		puts("cmd_line located > 16M\n");
	if ( (int)hold_residual > (16<<20))
		puts("hold_residual located > 16M\n");
	if ( initrd_start > (16<<20))
		puts("initrd_start located > 16M\n");

	puts("Uncompressing Linux...");
	
	gunzip(0, 0x400000, zimage_start, &zimage_size);
	puts("done.\n");
	
	{
		struct bi_record *rec;
	    
		rec = (struct bi_record *)_ALIGN((unsigned long)(zimage_size)+(1<<20)-1,(1<<20));
	    
		rec->tag = BI_FIRST;
		rec->size = sizeof(struct bi_record);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);

		rec->tag = BI_BOOTLOADER_ID;
		memcpy( (void *)rec->data, "prepboot", 9);
		rec->size = sizeof(struct bi_record) + 8 + 1;
		rec = (struct bi_record *)((unsigned long)rec + rec->size);
	    
		rec->tag = BI_MACHTYPE;
		rec->data[0] = _MACH_prep;
		rec->data[1] = 0;
		rec->size = sizeof(struct bi_record) + 2 * sizeof(unsigned long);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);
	    
		rec->tag = BI_CMD_LINE;
		memcpy( (char *)rec->data, cmd_line, strlen(cmd_line)+1);
		rec->size = sizeof(struct bi_record) + strlen(cmd_line) + 1;
		rec = (struct bi_record *)((ulong)rec + rec->size);
		
		rec->tag = BI_LAST;
		rec->size = sizeof(struct bi_record);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);
	}
	puts("Now booting the kernel\n");
	return (unsigned long)hold_residual;
}

/*
 * PCI/ISA I/O support
 */

volatile unsigned char *ISA_io  = (unsigned char *)0x80000000;
volatile unsigned char *ISA_mem = (unsigned char *)0xC0000000;

void
outb(int port, char val)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	ISA_io[port] = val;
}

unsigned char
inb(int port)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	return (ISA_io[port]);
}

unsigned long
local_to_PCI(unsigned long addr)
{
	return ((addr & 0x7FFFFFFF) | 0x80000000);
}

/*
 * misc.c
 *
 * $Id: misc.c,v 1.68 1999/10/20 22:08:08 cort Exp $
 * 
 * Adapted for PowerPC by Gary Thomas
 *
 * Rewritten by Cort Dougan (cort@cs.nmt.edu)
 * One day to be replaced by a single bootloader for chrp/prep/pmac. -- Cort
 */

#include <linux/types.h>
#include "../coffboot/zlib.h"
#include "asm/residual.h"
#include <linux/elf.h>
#include <linux/config.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/bootinfo.h>
#include <asm/mmu.h>
#if defined(CONFIG_SERIAL_CONSOLE)
#include "ns16550.h"
struct NS16550 *com_port;
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

void puts(const char *);
void putc(const char c);
void puthex(unsigned long val);
void _bcopy(char *src, char *dst, int len);
void * memcpy(void * __dest, __const void * __src,
			    int __n);
void gunzip(void *, int, unsigned char *, int *);
static int _cvt(unsigned long val, char *buf, long radix, char *digits);
unsigned char inb(int);

void pause()
{
	puts("pause\n");
}

void exit()
{
	puts("exit\n");
	while(1); 
}

static void clear_screen()
{
	int i, j;
	for (i = 0;  i < lines;  i++) {
	  for (j = 0;  j < cols;  j++) {
	    vidmem[((i*cols)+j)*2] = ' ';
	    vidmem[((i*cols)+j)*2+1] = 0x07;
	  }
	}
}

static void scroll()
{
	int i;

	memcpy ( vidmem, vidmem + cols * 2, ( lines - 1 ) * cols * 2 );
	for ( i = ( lines - 1 ) * cols * 2; i < lines * cols * 2; i += 2 )
		vidmem[i] = ' ';
}

tstc(void)
{
#if defined(CONFIG_SERIAL_CONSOLE)
	if (keyb_present)
		return (CRT_tstc() || NS16550_tstc(com_port));
	else
		NS16550_tstc(com_port);
#else
	return (CRT_tstc() );
#endif /* CONFIG_SERIAL_CONSOLE */
}

getc(void)
{
	while (1) {
#if defined(CONFIG_SERIAL_CONSOLE)
		if (NS16550_tstc(com_port)) return (NS16550_getc(com_port));
#endif /* CONFIG_SERIAL_CONSOLE */
		if (keyb_present)
			if (CRT_tstc()) return (CRT_getc());
	}
}

void 
putc(const char c)
{
	int x,y;

#if defined(CONFIG_SERIAL_CONSOLE)
	NS16550_putc(com_port, c);
	if ( c == '\n' ) NS16550_putc(com_port, '\r');
#endif /* CONFIG_SERIAL_CONSOLE */

	x = orig_x;
	y = orig_y;

	if ( c == '\n' ) {
		x = 0;
		if ( ++y >= lines ) {
			scroll();
			y--;
		}
	} else if (c == '\r') {
		x = 0;
	} else if (c == '\b') {
		if (x > 0) {
			x--;
		}
	} else {
		vidmem [ ( x + cols * y ) * 2 ] = c; 
		if ( ++x >= cols ) {
			x = 0;
			if ( ++y >= lines ) {
				scroll();
				y--;
			}
		}
	}

	cursor(x, y);

	orig_x = x;
	orig_y = y;
}

void puts(const char *s)
{
	int x,y;
	char c;

	x = orig_x;
	y = orig_y;

	while ( ( c = *s++ ) != '\0' ) {
#if defined(CONFIG_SERIAL_CONSOLE)
	        NS16550_putc(com_port, c);
	        if ( c == '\n' ) NS16550_putc(com_port, '\r');
#endif /* CONFIG_SERIAL_CONSOLE */

		if ( c == '\n' ) {
			x = 0;
			if ( ++y >= lines ) {
				scroll();
				y--;
			}
		} else if (c == '\b') {
		  if (x > 0) {
		    x--;
		  }
		} else {
			vidmem [ ( x + cols * y ) * 2 ] = c; 
			if ( ++x >= cols ) {
				x = 0;
				if ( ++y >= lines ) {
					scroll();
					y--;
				}
			}
		}
	}

	cursor(x, y);

	orig_x = x;
	orig_y = y;
}

void * memcpy(void * __dest, __const void * __src,
			    int __n)
{
	int i;
	char *d = (char *)__dest, *s = (char *)__src;

	for (i=0;i<__n;i++) d[i] = s[i];
}

int memcmp(__const void * __dest, __const void * __src,
			    int __n)
{
	int i;
	char *d = (char *)__dest, *s = (char *)__src;

	for (i=0;i<__n;i++, d++, s++)
	{
		if (*d != *s)
		{
			return (*s - *d);
		}
	}
	return (0);
}

void error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while(1);	/* Halt */
}

void *zalloc(void *x, unsigned items, unsigned size)
{
	void *p = avail_ram;
	
	size *= items;
	size = (size + 7) & -8;
	avail_ram += size;
	if (avail_ram > end_avail) {
		puts("oops... out of memory\n");
		pause();
	}
	return p;
}

void zfree(void *x, void *addr, unsigned nb)
{
}

#define HEAD_CRC	2
#define EXTRA_FIELD	4
#define ORIG_NAME	8
#define COMMENT		0x10
#define RESERVED	0xe0

#define DEFLATED	8


void gunzip(void *dst, int dstlen, unsigned char *src, int *lenp)
{
	z_stream s;
	int r, i, flags;
	
	/* skip header */
	i = 10;
	flags = src[3];
	if (src[2] != DEFLATED || (flags & RESERVED) != 0) {
		puts("bad gzipped data\n");
		exit();
	}
	if ((flags & EXTRA_FIELD) != 0)
		i = 12 + src[10] + (src[11] << 8);
	if ((flags & ORIG_NAME) != 0)
		while (src[i++] != 0)
			;
	if ((flags & COMMENT) != 0)
		while (src[i++] != 0)
			;
	if ((flags & HEAD_CRC) != 0)
		i += 2;
	if (i >= *lenp) {
		puts("gunzip: ran out of data in header\n");
		exit();
	}
	
	s.zalloc = zalloc;
	s.zfree = zfree;
	r = inflateInit2(&s, -MAX_WBITS);
	if (r != Z_OK) {
		puts("inflateInit2 returned %d\n");
		exit();
	}
	s.next_in = src + i;
	s.avail_in = *lenp - i;
	s.next_out = dst;
	s.avail_out = dstlen;
	r = inflate(&s, Z_FINISH);
	if (r != Z_OK && r != Z_STREAM_END) {
		puts("inflate returned %d\n");
		exit();
	}
	*lenp = s.next_out - (unsigned char *) dst;
	inflateEnd(&s);
}

unsigned long
decompress_kernel(unsigned long load_addr, int num_words, unsigned long cksum,
		  RESIDUAL *residual, void *OFW_interface)
{
	int timer;
	extern unsigned long start;
	char *cp, ch;
	unsigned long i;
	BATU *u;
	BATL *l;
	unsigned long TotalMemory;
	unsigned long orig_MSR;
	int dev_handle;
	int mem_info[2];
	int res, size;
	unsigned char board_type;
	unsigned char base_mod;

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
	com_port = (struct NS16550 *)NS16550_init(0);
#endif /* CONFIG_SERIAL_CONSOLE */
	vga_init(0xC0000000);

	if (residual)
	{
		/* Is this Motorola PPCBug? */
		if ((1 & residual->VitalProductData.FirmwareSupports) &&
		    (1 == residual->VitalProductData.FirmwareSupplier)) {
			board_type = inb(0x800) & 0xF0;

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
		avail_ram += zimage_size;
	}

	/* relocate initrd */
	if ( initrd_start )
	{
		puts("initrd at:     "); puthex(initrd_start);
		puts(" "); puthex(initrd_end); puts("\n");
		if ( (unsigned long)initrd_start <= 0x00800000 )
		{
			memcpy( (void *)avail_ram,
				(void *)initrd_start, initrd_end-initrd_start );
			puts("relocated to:  ");
			initrd_end = (unsigned long) avail_ram + (initrd_end-initrd_start);
			initrd_start = (unsigned long)avail_ram;
			puthex((unsigned long)initrd_start);
			puts(" ");
			puthex((unsigned long)initrd_end);
			puts("\n");
		}
		avail_ram = (char *)PAGE_ALIGN((unsigned long)initrd_end);
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
				if (ch == '\b') {
					if (cp != cmd_line) {
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

	puts("Uncompressing Linux...");
	gunzip(0, 0x400000, zimage_start, &zimage_size);
	puts("done.\n");
	
	{
		struct bi_record *rec;
	    
		rec = (struct bi_record *)PAGE_ALIGN(zimage_size);
	    
		rec->tag = BI_FIRST;
		rec->size = sizeof(struct bi_record);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);

		rec->tag = BI_BOOTLOADER_ID;
		memcpy( (void *)rec->data, "prepboot", 9);
		rec->size = sizeof(struct bi_record) + 8 + 1;
		rec = (struct bi_record *)((unsigned long)rec + rec->size);
	    
		rec->tag = BI_MACHTYPE;
		rec->data[0] = _MACH_prep;
		rec->data[1] = 1;
		rec->size = sizeof(struct bi_record) + sizeof(unsigned long);
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

void puthex(unsigned long val)
{
	unsigned char buf[10];
	int i;
	for (i = 7;  i >= 0;  i--)
	{
		buf[i] = "0123456789ABCDEF"[val & 0x0F];
		val >>= 4;
	}
	buf[8] = '\0';
	puts(buf);
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

void
_bcopy(char *src, char *dst, int len)
{
	while (len--) *dst++ = *src++;
}


#define FALSE 0
#define TRUE  1
#include <stdarg.h>

int
strlen(char *s)
{
	int len = 0;
	while (*s++) len++;
	return len;
}

_printk(char const *fmt, ...)
{
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = _vprintk(putc, fmt, ap);
	va_end(ap);
	return (ret);
}

#define is_digit(c) ((c >= '0') && (c <= '9'))

int
_vprintk(putc, fmt0, ap)
int (*putc)();
const char *fmt0;
va_list ap;
{
	char c, sign, *cp;
	int left_prec, right_prec, zero_fill, length, pad, pad_on_right;
	char buf[32];
	long val;
	while (c = *fmt0++)
	{
		if (c == '%')
		{
			c = *fmt0++;
			left_prec = right_prec = pad_on_right = 0;
			if (c == '-')
			{
				c = *fmt0++;
				pad_on_right++;
			}
			if (c == '0')
			{
				zero_fill = TRUE;
				c = *fmt0++;
			} else
			{
				zero_fill = FALSE;
			}
			while (is_digit(c))
			{
				left_prec = (left_prec * 10) + (c - '0');
				c = *fmt0++;
			}
			if (c == '.')
			{
				c = *fmt0++;
				zero_fill++;
				while (is_digit(c))
				{
					right_prec = (right_prec * 10) + (c - '0');
					c = *fmt0++;
				}
			} else
			{
				right_prec = left_prec;
			}
			sign = '\0';
			switch (c)
			{
			case 'd':
			case 'x':
			case 'X':
				val = va_arg(ap, long);
				switch (c)
				{
				case 'd':
					if (val < 0)
					{
						sign = '-';
						val = -val;
					}
					length = _cvt(val, buf, 10, "0123456789");
					break;
				case 'x':
					length = _cvt(val, buf, 16, "0123456789abcdef");
					break;
				case 'X':
					length = _cvt(val, buf, 16, "0123456789ABCDEF");
					break;
				}
				cp = buf;
				break;
			case 's':
				cp = va_arg(ap, char *);
				length = strlen(cp);
				break;
			case 'c':
				c = va_arg(ap, long /*char*/);
				(*putc)(c);
				continue;
			default:
				(*putc)('?');
			}
			pad = left_prec - length;
			if (sign != '\0')
			{
				pad--;
			}
			if (zero_fill)
			{
				c = '0';
				if (sign != '\0')
				{
					(*putc)(sign);
					sign = '\0';
				}
			} else
			{
				c = ' ';
			}
			if (!pad_on_right)
			{
				while (pad-- > 0)
				{
					(*putc)(c);
				}
			}
			if (sign != '\0')
			{
				(*putc)(sign);
			}
			while (length-- > 0)
			{
				(*putc)(c = *cp++);
				if (c == '\n')
				{
					(*putc)('\r');
				}
			}
			if (pad_on_right)
			{
				while (pad-- > 0)
				{
					(*putc)(c);
				}
			}
		} else
		{
			(*putc)(c);
			if (c == '\n')
			{
				(*putc)('\r');
			}
		}
	}
}

int _cvt(unsigned long val, char *buf, long radix, char *digits)
{
	char temp[80];
	char *cp = temp;
	int length = 0;
	if (val == 0)
	{ /* Special case */
		*cp++ = '0';
	} else
		while (val)
		{
			*cp++ = digits[val % radix];
			val /= radix;
		}
	while (cp != temp)
	{
		*buf++ = *--cp;
		length++;
	}
	*buf = '\0';
	return (length);
}

_dump_buf_with_offset(unsigned char *p, int s, unsigned char *base)
{
	int i, c;
	if ((unsigned int)s > (unsigned int)p)
	{
		s = (unsigned int)s - (unsigned int)p;
	}
	while (s > 0)
	{
		if (base)
		{
			_printk("%06X: ", (int)p - (int)base);
		} else
		{
			_printk("%06X: ", p);
		}
		for (i = 0;  i < 16;  i++)
		{
			if (i < s)
			{
				_printk("%02X", p[i] & 0xFF);
			} else
			{
				_printk("  ");
			}
			if ((i % 2) == 1) _printk(" ");
			if ((i % 8) == 7) _printk(" ");
		}
		_printk(" |");
		for (i = 0;  i < 16;  i++)
		{
			if (i < s)
			{
				c = p[i] & 0xFF;
				if ((c < 0x20) || (c >= 0x7F)) c = '.';
			} else
			{
				c = ' ';
			}
			_printk("%c", c);
		}
		_printk("|\n");
		s -= 16;
		p += 16;
	}
}

_dump_buf(unsigned char *p, int s)
{
	_printk("\n");
	_dump_buf_with_offset(p, s, 0);
}

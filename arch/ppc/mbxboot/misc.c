/*
 * misc.c
 *
 * $Id: misc.c,v 1.2 1999/09/14 05:55:29 dmalek Exp $
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
#include <asm/mmu.h>
#ifdef CONFIG_8xx
#include <asm/mpc8xx.h>
#endif
#ifdef CONFIG_8260
#include <asm/mpc8260.h>
#endif

/*
 * The following references are needed to cause the linker to pull in the
 * gzimage.o and rdimage.o files.  These object files are special,
 * since they get placed into the .gzimage and .rdimage ELF sections 
 * of the zvmlinux and zvmlinux.initrd files.
 */
extern char dummy_for_gzimage;
extern char dummy_for_rdimage;

/*
 * Please send me load/board info and such data for hardware not
 * listed here so I can keep track since things are getting tricky
 * with the different load addrs with different firmware.  This will
 * help to avoid breaking the load/boot process.
 * -- Cort
 */
char *avail_ram;
char *end_avail;

/* See comment below.....
*/
unsigned int initrd_offset, initrd_size;

/* Because of the limited amount of memory on embedded, it presents
 * loading problems.  The biggest is that we load this boot program
 * into a relatively low memory address, and the Linux kernel Bss often
 * extends into this space when it get loaded.  When the kernel starts
 * and zeros the BSS space, it also writes over the information we
 * save here and pass to the kernel (command line and board info).
 * On these boards, we grab some known memory holes to hold this information.
 */
char	cmd_buf[256];
char	*cmd_line = cmd_buf;

char	*root_string = "root=/dev/nfs rw";
char	*nfsaddrs_string = "nfsaddrs=";
char	*nfsroot_string = "nfsroot=";
char	*defroot_string = "/sys/mbxroot";
char	*ramroot_string = "root=/dev/ram";
int	do_ipaddrs(char **cmd_cp, int echo);
void	do_nfsroot(char **cmd_cp, char *dp);
int	strncmp(const char * cs,const char * ct,size_t count);
char	*strrchr(const char * s, int c);

bd_t hold_resid_buf;
bd_t *hold_residual = &hold_resid_buf;
unsigned long initrd_start = 0, initrd_end = 0;
char *zimage_start;
int zimage_size;

void puts(const char *);
void putc(const char c);
void puthex(unsigned long val);
void _bcopy(char *src, char *dst, int len);
void * memcpy(void * __dest, __const void * __src,
			    int __n);
void gunzip(void *, int, unsigned char *, int *);

void pause()
{
	puts("pause\n");
}

void exit()
{
	puts("exit\n");
	while(1); 
}

/* The MPC8xx is just the serial port.
*/
tstc(void)
{
        return (serial_tstc());
}

getc(void)
{
        while (1) {
                if (serial_tstc()) return (serial_getc());
        }
}

void 
putc(const char c)
{
        serial_putchar(c);
}

void puts(const char *s)
{
        char c;

        while ( ( c = *s++ ) != '\0' ) {
                serial_putchar(c);
                if ( c == '\n' )
                        serial_putchar('\r');
        }
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
		puts("inflate returned ");
		puthex(r);
		puts("\n");
		exit();
	}
	*lenp = s.next_out - (unsigned char *) dst;
	inflateEnd(&s);
}

unsigned char sanity[0x2000];

unsigned long
decompress_kernel(unsigned long load_addr, int num_words, unsigned long cksum, bd_t *bp)
{
	int timer;
	extern unsigned long start;
	char *cp, ch;
	unsigned long i;
	char	*dp;

#ifdef CONFIG_8260
	/* I don't know why I didn't do it this way on the 8xx.......
	*/
	embed_config(bp);
	serial_init(bp);
#endif

	/* These values must be variables.  If not, the compiler optimizer
	 * will remove some code, causing the size of the code to vary
	 * when these values are zero.  This is bad because we first
	 * compile with these zero to determine the size and offsets
	 * in an image, than compile again with these set to the proper
	 * discovered value.....Ya know, we used to read these from the
	 * header a long time ago.....
	 */
	initrd_offset = INITRD_OFFSET;
	initrd_size = INITRD_SIZE;

	/* Grab some space for the command line and board info.  Since
	 * we no longer use the ELF header, but it was loaded, grab
	 * that space.
	 */
#ifdef CONFIG_MBX
	cmd_line = (char *)(load_addr - 0x10000);

	/* To be like everyone else, we need one too, although this
	 * board information is passed from the boot rom.
	 */
	bp->bi_baudrate = 9600;
#else
	cmd_line = (char *)(0x200000);
#endif
	hold_residual = (bd_t *)(cmd_line + sizeof(cmd_buf));
	/* copy board data */
	if (bp)
		memcpy(hold_residual,bp,sizeof(bd_t));

	/* Set end of memory available to us.  It is always the highest
	 * memory address provided by the board information.
	 */
	end_avail = (char *)(bp->bi_memsize);

	puts("loaded at:     "); puthex(load_addr);
	puts(" "); puthex((unsigned long)(load_addr + (4*num_words))); puts("\n");
	if ( (unsigned long)load_addr != (unsigned long)&start )
	{
		puts("relocated to:  "); puthex((unsigned long)&start);
		puts(" ");
		puthex((unsigned long)((unsigned long)&start + (4*num_words)));
		puts("\n");
	}

	if ( bp )
	{
		puts("board data at: "); puthex((unsigned long)bp);
		puts(" ");
		puthex((unsigned long)((unsigned long)bp + sizeof(bd_t)));
		puts("\n");
		puts("relocated to:  ");
		puthex((unsigned long)hold_residual);
		puts(" ");
		puthex((unsigned long)((unsigned long)hold_residual + sizeof(bd_t)));
		puts("\n");
	}

	/* we have to subtract 0x10000 here to correct for objdump including the
	   size of the elf header which we strip -- Cort */
	zimage_start = (char *)(load_addr - 0x10000 + ZIMAGE_OFFSET);
	zimage_size = ZIMAGE_SIZE;

	if ( initrd_offset )
		initrd_start = load_addr - 0x10000 + initrd_offset;
	else
		initrd_start = 0;
	initrd_end = initrd_size + initrd_start;

	/*
	 * setup avail_ram - this is the first part of ram usable
	 * by the uncompress code. -- Cort
	 */
	avail_ram = (char *)PAGE_ALIGN((unsigned long)zimage_start+zimage_size);
	if ( ((load_addr+(num_words*4)) > (unsigned long) avail_ram)
		&& (load_addr <= 0x01000000) )
		avail_ram = (char *)(load_addr+(num_words*4));
	if ( (((unsigned long)&start+(num_words*4)) > (unsigned long) avail_ram)
		&& (load_addr <= 0x01000000) )
		avail_ram = (char *)((unsigned long)&start+(num_words*4));
	
	/* relocate zimage */
	puts("zimage at:     "); puthex((unsigned long)zimage_start);
	puts(" "); puthex((unsigned long)(zimage_size+zimage_start)); puts("\n");
	/*
	 * There is no reason (yet) to relocate zImage for embedded boards.
	 * To support boot from flash rom on 8xx embedded boards, I
	 * assume if zimage start is over 16M we are booting from flash.
	 * In this case, avilable ram will start just above the space we
	 * have allocated for the command buffer and board information.
	 */
	if ((unsigned long)zimage_start > 0x01000000)
		avail_ram = (char *)PAGE_ALIGN((unsigned long)hold_residual + sizeof(bd_t));

	/* relocate initrd */
	if ( initrd_start )
	{
		puts("initrd at:     "); puthex(initrd_start);
		puts(" "); puthex(initrd_end); puts("\n");

		/* We only have to relocate initrd if we find it is in Flash
		 * rom.  This is because the kernel thinks it can toss the
		 * pages into the free memory pool after it is done.  Use
		 * the same 16M test.
		 */
		if ((unsigned long)initrd_start > 0x01000000) {
			memcpy ((void *)PAGE_ALIGN(-PAGE_SIZE+(unsigned long)end_avail-INITRD_SIZE),
				(void *)initrd_start,
				initrd_size );
			initrd_start = PAGE_ALIGN(-PAGE_SIZE+(unsigned long)end_avail-INITRD_SIZE);
			initrd_end = initrd_start + initrd_size;
			end_avail = (char *)initrd_start;
			puts("relocated to:  "); puthex(initrd_start);
			puts(" "); puthex(initrd_end); puts("\n");
		}
		else {
			avail_ram = (char *)PAGE_ALIGN((unsigned long)initrd_end);
		}
	}


	puts("avail ram:     "); puthex((unsigned long)avail_ram); puts(" ");
	puthex((unsigned long)end_avail); puts("\n");

	puts("\nLinux/PPC load: ");
	timer = 0;
	cp = cmd_line;

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

	/* If the command line is not filled in, we will automatically
	 * create the default boot.
	 */
	if (cmd_line[0] == 0) {

		/* An initrd on these boards means we booted from Flash
		 * ROM and want to use the ramdisk as the root file system.
		 * Otherwise, we perform a diskless NFS boot.
		 */
		if (initrd_start)
			dp = ramroot_string;
		else
			dp = root_string;
		while (*dp != 0)
			*cp++ = *dp++;
		*cp = 0;
	}

	puts("\n");

	puts("Uncompressing Linux...");

	gunzip(0, 0x400000, zimage_start, &zimage_size);
	puts("done.\n");
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

/*
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/config.h>
#include <linux/string.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/page.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/cuda.h>
#include <linux/kernel.h>
#include <asm/prom.h>
#include <asm/bootx.h>
#include <asm/feature.h>
#include <asm/processor.h>

static volatile unsigned char *sccc, *sccd;
unsigned long TXRDY, RXRDY;
extern void xmon_printf(const char *fmt, ...);
extern void prom_drawchar(char);
extern void prom_drawstring(const char *str);
static int xmon_expect(const char *str, unsigned int timeout);

static int console = 0;
static int use_screen = 1; /* default */
static int via_modem = 0;
static int xmon_use_sccb = 0;
static struct device_node *macio_node;

#define TB_SPEED	25000000

static inline unsigned int readtb(void)
{
	unsigned int ret;

	asm volatile("mftb %0" : "=r" (ret) :);
	return ret;
}

void buf_access(void)
{
	if ( _machine == _MACH_chrp )
		sccd[3] &= ~0x80;	/* reset DLAB */
}

extern int adb_init(void);

void
xmon_map_scc(void)
{
	volatile unsigned char *base;

	use_screen = 0;
	
	if ( _machine == _MACH_Pmac )
	{
		struct device_node *np;
		unsigned long addr;
#ifdef CONFIG_BOOTX_TEXT
		extern boot_infos_t *disp_bi;

		/* needs to be hacked if xmon_printk is to be used
 		   from within find_via_pmu() */
#ifdef CONFIG_ADB_PMU
		if (!via_modem && disp_bi && find_via_pmu()) {
			prom_drawstring("xmon uses screen and keyboard\n");
			use_screen = 1;
		}
#endif
#ifdef CONFIG_ADB_CUDA
		if (!via_modem && disp_bi ) {
			prom_drawstring("xmon uses screen and keyboard\n");
			use_screen = 1;
		}
#endif
#endif

#ifdef CHRP_ESCC
		addr = 0xc1013020;
#else
		addr = 0xf3013020;
#endif
		TXRDY = 4;
		RXRDY = 1;
		
		np = find_devices("mac-io");
		if (np && np->n_addrs) {
			macio_node = np;
			addr = np->addrs[0].address + 0x13020;
		}
		base = (volatile unsigned char *) ioremap(addr & PAGE_MASK, PAGE_SIZE);
		sccc = base + (addr & ~PAGE_MASK);
		sccd = sccc + 0x10;
	}
	else if ( _machine & _MACH_gemini )
	{
		/* should already be mapped by the kernel boot */
		sccc = (volatile unsigned char *) 0xffeffb0d;
		sccd = (volatile unsigned char *) 0xffeffb08;
		TXRDY = 0x20;
		RXRDY = 1;
		console = 1;
	}
	else
	{
		/* should already be mapped by the kernel boot */
		sccc = (volatile unsigned char *) (isa_io_base + 0x3fd);
		sccd = (volatile unsigned char *) (isa_io_base + 0x3f8);
		if (xmon_use_sccb) {
			sccc -= 0x100;
			sccd -= 0x100;
		}
		TXRDY = 0x20;
		RXRDY = 1;
	}
}

static int scc_initialized = 0;

void xmon_init_scc(void);
extern void pmu_poll(void);
extern void cuda_poll(void);

static inline void do_poll_adb(void)
{
#ifdef CONFIG_ADB_PMU
	if (sys_ctrler == SYS_CTRLER_PMU)
		pmu_poll();
#endif /* CONFIG_ADB_PMU */
#ifdef CONFIG_ADB_CUDA
	if (sys_ctrler == SYS_CTRLER_CUDA)
		cuda_poll();
#endif /* CONFIG_ADB_CUDA */
}

int
xmon_write(void *handle, void *ptr, int nb)
{
	char *p = ptr;
	int i, c, ct;

#ifdef CONFIG_BOOTX_TEXT
	if (use_screen) {
		/* write it on the screen */
		for (i = 0; i < nb; ++i)
			prom_drawchar(*p++);
		return nb;
	}
#endif
	if (!scc_initialized)
		xmon_init_scc();
	ct = 0;
	for (i = 0; i < nb; ++i) {
		while ((*sccc & TXRDY) == 0)
			do_poll_adb();
		c = p[i];
		if (c == '\n' && !ct) {
			c = '\r';
			ct = 1;
			--i;
		} else {
			if (console)
				printk("%c", c);
			ct = 0;
		}
		buf_access();
		*sccd = c;
	}
	return i;
}

int xmon_wants_key;
int xmon_adb_keycode;

#ifdef CONFIG_BOOTX_TEXT
static int xmon_adb_shiftstate;

static unsigned char xmon_keytab[128] =
	"asdfhgzxcv\000bqwer"				/* 0x00 - 0x0f */
	"yt123465=97-80o]"				/* 0x10 - 0x1f */
	"u[ip\rlj'k;\\,/nm."				/* 0x20 - 0x2f */
	"\t `\177\0\033\0\0\0\0\0\0\0\0\0\0"		/* 0x30 - 0x3f */
	"\0.\0*\0+\0\0\0\0\0/\r\0-\0"			/* 0x40 - 0x4f */
	"\0\0000123456789\0\0\0";			/* 0x50 - 0x5f */

static unsigned char xmon_shift_keytab[128] =
	"ASDFHGZXCV\000BQWER"				/* 0x00 - 0x0f */
	"YT!@#$^%+(&=*)}O"				/* 0x10 - 0x1f */
	"U{IP\rLJ\"K:|<?NM>"				/* 0x20 - 0x2f */
	"\t ~\177\0\033\0\0\0\0\0\0\0\0\0\0"		/* 0x30 - 0x3f */
	"\0.\0*\0+\0\0\0\0\0/\r\0-\0"			/* 0x40 - 0x4f */
	"\0\0000123456789\0\0\0";			/* 0x50 - 0x5f */

static int
xmon_get_adb_key(void)
{
	int k, t, on;

	xmon_wants_key = 1;
	for (;;) {
		xmon_adb_keycode = -1;
		t = 0;
		on = 0;
		do {
			if (--t < 0) {
				on = 1 - on;
				prom_drawchar(on? 0xdb: 0x20);
				prom_drawchar('\b');
				t = 200000;
			}
			do_poll_adb();
		} while (xmon_adb_keycode == -1);
		k = xmon_adb_keycode;
		if (on)
			prom_drawstring(" \b");

		/* test for shift keys */
		if ((k & 0x7f) == 0x38 || (k & 0x7f) == 0x7b) {
			xmon_adb_shiftstate = (k & 0x80) == 0;
			continue;
		}
		if (k >= 0x80)
			continue;	/* ignore up transitions */
		k = (xmon_adb_shiftstate? xmon_shift_keytab: xmon_keytab)[k];
		if (k != 0)
			break;
	}
	xmon_wants_key = 0;
	return k;
}
#endif /* CONFIG_BOOTX_TEXT */

int
xmon_read(void *handle, void *ptr, int nb)
{
    char *p = ptr;
    int i;

#ifdef CONFIG_BOOTX_TEXT
    if (use_screen) {
	for (i = 0; i < nb; ++i)
	    *p++ = xmon_get_adb_key();
	return i;
    }
#endif
    if (!scc_initialized)
	xmon_init_scc();
    for (i = 0; i < nb; ++i) {
	while ((*sccc & RXRDY) == 0)
	    do_poll_adb();
	buf_access();
	*p++ = *sccd;
    }
    return i;
}

int
xmon_read_poll(void)
{
	if ((*sccc & RXRDY) == 0) {
		do_poll_adb();
		return -1;
	}
	buf_access();
	return *sccd;
}

static unsigned char scc_inittab[] = {
    13, 0,		/* set baud rate divisor */
    12, 1,
    14, 1,		/* baud rate gen enable, src=rtxc */
    11, 0x50,		/* clocks = br gen */
    5,  0xea,		/* tx 8 bits, assert DTR & RTS */
    4,  0x46,		/* x16 clock, 1 stop */
    3,  0xc1,		/* rx enable, 8 bits */
};

void
xmon_init_scc()
{
	if ( _machine == _MACH_chrp )
	{
		sccd[3] = 0x83; eieio();	/* LCR = 8N1 + DLAB */
		sccd[0] = 12; eieio();		/* DLL = 9600 baud */
		sccd[1] = 0; eieio();
		sccd[2] = 0; eieio();		/* FCR = 0 */
		sccd[3] = 3; eieio();		/* LCR = 8N1 */
		sccd[1] = 0; eieio();		/* IER = 0 */
	}
	else if ( _machine == _MACH_Pmac )
	{
		int i, x;

		if (macio_node != 0)
			feature_set(macio_node, FEATURE_Serial_enable);
		if (via_modem && macio_node != 0) {
			unsigned int t0;

			feature_set(macio_node, FEATURE_Modem_power);
			t0 = readtb();
			while (readtb() - t0 < 3*TB_SPEED)
				eieio();
		}
		/* use the B channel if requested */
		if (xmon_use_sccb) {
			sccc = (volatile unsigned char *)
				((unsigned long)sccc & ~0x20);
			sccd = sccc + 0x10;
		}
		for (i = 20000; i != 0; --i) {
			x = *sccc; eieio();
		}
		*sccc = 9; eieio();		/* reset A or B side */
		*sccc = ((unsigned long)sccc & 0x20)? 0x80: 0x40; eieio();
		for (i = 0; i < sizeof(scc_inittab); ++i) {
			*sccc = scc_inittab[i];
			eieio();
		}
	}
	scc_initialized = 1;
	if (via_modem) {
		for (;;) {
			xmon_write(0, "ATE1V1\r", 7);
			if (xmon_expect("OK", 5)) {
				xmon_write(0, "ATA\r", 4);
				if (xmon_expect("CONNECT", 40))
					break;
			}
			xmon_write(0, "+++", 3);
			xmon_expect("OK", 3);
		}
	}
}

#if 0
extern int (*prom_entry)(void *);

int
xmon_exit(void)
{
    struct prom_args {
	char *service;
    } args;

    for (;;) {
	args.service = "exit";
	(*prom_entry)(&args);
    }
}
#endif

void *xmon_stdin;
void *xmon_stdout;
void *xmon_stderr;

void
xmon_init(void)
{
}

int
xmon_putc(int c, void *f)
{
    char ch = c;

    if (c == '\n')
	xmon_putc('\r', f);
    return xmon_write(f, &ch, 1) == 1? c: -1;
}

int
xmon_putchar(int c)
{
    return xmon_putc(c, xmon_stdout);
}

int
xmon_fputs(char *str, void *f)
{
    int n = strlen(str);

    return xmon_write(f, str, n) == n? 0: -1;
}

int
xmon_readchar(void)
{
    char ch;

    for (;;) {
	switch (xmon_read(xmon_stdin, &ch, 1)) {
	case 1:
	    return ch;
	case -1:
	    xmon_printf("read(stdin) returned -1\r\n", 0, 0);
	    return -1;
	}
    }
}

static char line[256];
static char *lineptr;
static int lineleft;

int xmon_expect(const char *str, unsigned int timeout)
{
	int c;
	unsigned int t0;

	timeout *= TB_SPEED;
	t0 = readtb();
	do {
		lineptr = line;
		for (;;) {
			c = xmon_read_poll();
			if (c == -1) {
				if (readtb() - t0 > timeout)
					return 0;
				continue;
			}
			if (c == '\n')
				break;
			if (c != '\r' && lineptr < &line[sizeof(line) - 1])
				*lineptr++ = c;
		}
		*lineptr = 0;
	} while (strstr(line, str) == NULL);
	return 1;
}

int
xmon_getchar(void)
{
    int c;

    if (lineleft == 0) {
	lineptr = line;
	for (;;) {
	    c = xmon_readchar();
	    if (c == -1 || c == 4)
		break;
	    if (c == '\r' || c == '\n') {
		*lineptr++ = '\n';
		xmon_putchar('\n');
		break;
	    }
	    switch (c) {
	    case 0177:
	    case '\b':
		if (lineptr > line) {
		    xmon_putchar('\b');
		    xmon_putchar(' ');
		    xmon_putchar('\b');
		    --lineptr;
		}
		break;
	    case 'U' & 0x1F:
		while (lineptr > line) {
		    xmon_putchar('\b');
		    xmon_putchar(' ');
		    xmon_putchar('\b');
		    --lineptr;
		}
		break;
	    default:
		if (lineptr >= &line[sizeof(line) - 1])
		    xmon_putchar('\a');
		else {
		    xmon_putchar(c);
		    *lineptr++ = c;
		}
	    }
	}
	lineleft = lineptr - line;
	lineptr = line;
    }
    if (lineleft == 0)
	return -1;
    --lineleft;
    return *lineptr++;
}

char *
xmon_fgets(char *str, int nb, void *f)
{
    char *p;
    int c;

    for (p = str; p < str + nb - 1; ) {
	c = xmon_getchar();
	if (c == -1) {
	    if (p == str)
		return 0;
	    break;
	}
	*p++ = c;
	if (c == '\n')
	    break;
    }
    *p = 0;
    return str;
}

void
xmon_enter(void)
{
#ifdef CONFIG_ADB_PMU
	pmu_suspend();
#endif
}

void
xmon_leave(void)
{
#ifdef CONFIG_ADB_PMU
	pmu_resume();
#endif
}

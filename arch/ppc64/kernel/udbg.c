/*
 * NS16550 Serial Port (uart) debugging stuff.
 *
 * c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#define WANT_PPCDBG_TAB /* Only defined here */
#include <asm/ppcdebug.h>
#include <asm/processor.h>
#include <asm/naca.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pmac_feature.h>

struct NS16550 {
	/* this struct must be packed */
	unsigned char rbr;  /* 0 */
	unsigned char ier;  /* 1 */
	unsigned char fcr;  /* 2 */
	unsigned char lcr;  /* 3 */
	unsigned char mcr;  /* 4 */
	unsigned char lsr;  /* 5 */
	unsigned char msr;  /* 6 */
	unsigned char scr;  /* 7 */
};

#define thr rbr
#define iir fcr
#define dll rbr
#define dlm ier
#define dlab lcr

#define LSR_DR   0x01  /* Data ready */
#define LSR_OE   0x02  /* Overrun */
#define LSR_PE   0x04  /* Parity error */
#define LSR_FE   0x08  /* Framing error */
#define LSR_BI   0x10  /* Break */
#define LSR_THRE 0x20  /* Xmit holding register empty */
#define LSR_TEMT 0x40  /* Xmitter empty */
#define LSR_ERR  0x80  /* Error */

static volatile struct NS16550 *udbg_comport;

void udbg_init_uart(void *comport)
{
	if (comport) {
		udbg_comport = (struct NS16550 *)comport;
		udbg_comport->lcr = 0x00; eieio();
		udbg_comport->ier = 0xFF; eieio();
		udbg_comport->ier = 0x00; eieio();
		udbg_comport->lcr = 0x80; eieio();	/* Access baud rate */
		udbg_comport->dll = 12;   eieio();	/* 1 = 115200,  2 = 57600, 3 = 38400, 12 = 9600 baud */
		udbg_comport->dlm = 0;    eieio();	/* dll >> 8 which should be zero for fast rates; */
		udbg_comport->lcr = 0x03; eieio();	/* 8 data, 1 stop, no parity */
		udbg_comport->mcr = 0x03; eieio();	/* RTS/DTR */
		udbg_comport->fcr = 0x07; eieio();	/* Clear & enable FIFOs */
	}
}

#ifdef CONFIG_PPC_PMAC

#define	SCC_TXRDY	4
#define SCC_RXRDY	1

static volatile u8 *sccc, *sccd;

static unsigned char scc_inittab[] = {
    13, 0,		/* set baud rate divisor */
    12, 0,
    14, 1,		/* baud rate gen enable, src=rtxc */
    11, 0x50,		/* clocks = br gen */
    5,  0xea,		/* tx 8 bits, assert DTR & RTS */
    4,  0x46,		/* x16 clock, 1 stop */
    3,  0xc1,		/* rx enable, 8 bits */
};

void udbg_init_scc(struct device_node *np)
{
	unsigned long addr;
	int i, x;

	if (np == NULL)
		np = of_find_node_by_name(NULL, "escc");
	if (np == NULL)
		return;
	
	/* Lock-enable the SCC channel */
	pmac_call_feature(PMAC_FTR_SCC_ENABLE, np, PMAC_SCC_ASYNC | PMAC_SCC_FLAG_XMON, 1);

	/* Setup for 57600 8N1 */
	addr = np->addrs[0].address + 0x20;
	sccc = (volatile u8 *) ioremap(addr & PAGE_MASK, PAGE_SIZE) ;
	sccc += addr & ~PAGE_MASK;
	sccd = sccc + 0x10;

	for (i = 20000; i != 0; --i)
		x = *sccc; eieio();
	*sccc = 9; eieio();		/* reset A or B side */
	*sccc = 0xc0; eieio();
	for (i = 0; i < sizeof(scc_inittab); ++i) {
		*sccc = scc_inittab[i];
		eieio();
	}

	ppc_md.udbg_putc = udbg_putc;
	ppc_md.udbg_getc = udbg_getc;
	ppc_md.udbg_getc_poll = udbg_getc_poll;

	udbg_puts("Hello World !\n");
}

#endif /* CONFIG_PPC_PMAC */

void udbg_putc(unsigned char c)
{
	if (udbg_comport) {
		while ((udbg_comport->lsr & LSR_THRE) == 0)
			/* wait for idle */;
		udbg_comport->thr = c; eieio();
		if (c == '\n') {
			/* Also put a CR.  This is for convenience. */
			while ((udbg_comport->lsr & LSR_THRE) == 0)
				/* wait for idle */;
			udbg_comport->thr = '\r'; eieio();
		}
	}
#ifdef CONFIG_PPC_PMAC
	else if (sccc) {
		while ((*sccc & SCC_TXRDY) == 0)
			eieio();
		*sccd = c;		
		eieio();
		if (c == '\n')
			udbg_putc('\r');
	}
#endif /* CONFIG_PPC_PMAC */
}

int udbg_getc_poll(void)
{
	if (udbg_comport) {
		if ((udbg_comport->lsr & LSR_DR) != 0)
			return udbg_comport->rbr;
		else
			return -1;
	}
#ifdef CONFIG_PPC_PMAC
	else if (sccc) {
		eieio();
		if ((*sccc & SCC_RXRDY) != 0)
			return *sccd;
		else
			return -1;
	}
#endif /* CONFIG_PPC_PMAC */
	return -1;
}

unsigned char udbg_getc(void)
{
	if (udbg_comport) {
		while ((udbg_comport->lsr & LSR_DR) == 0)
			/* wait for char */;
		return udbg_comport->rbr;
	}
#ifdef CONFIG_PPC_PMAC
	else if (sccc) {
		eieio();
		while ((*sccc & SCC_RXRDY) == 0)
			eieio();
		return *sccd;
	}
#endif /* CONFIG_PPC_PMAC */
	return 0;
}

void udbg_puts(const char *s)
{
	if (ppc_md.udbg_putc) {
		char c;

		if (s && *s != '\0') {
			while ((c = *s++) != '\0')
				ppc_md.udbg_putc(c);
		}
	} else {
		printk("%s", s);
	}
}

int udbg_write(const char *s, int n)
{
	int remain = n;
	char c;

	if (!ppc_md.udbg_putc)
		return 0;

	if (s && *s != '\0') {
		while (((c = *s++) != '\0') && (remain-- > 0)) {
			ppc_md.udbg_putc(c);
		}
	}

	return n - remain;
}

int udbg_read(char *buf, int buflen)
{
	char c, *p = buf;
	int i;

	if (!ppc_md.udbg_putc)
		return 0;

	for (i = 0; i < buflen; ++i) {
		do {
			c = ppc_md.udbg_getc();
		} while (c == 0x11 || c == 0x13);
		if (c == 0)
			break;
		*p++ = c;
	}

	return i;
}

void udbg_console_write(struct console *con, const char *s, unsigned int n)
{
	udbg_write(s, n);
}

#define UDBG_BUFSIZE 256
void udbg_printf(const char *fmt, ...)
{
	unsigned char buf[UDBG_BUFSIZE];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, UDBG_BUFSIZE, fmt, args);
	udbg_puts(buf);
	va_end(args);
}

/* Special print used by PPCDBG() macro */
void udbg_ppcdbg(unsigned long debug_flags, const char *fmt, ...)
{
	unsigned long active_debugs = debug_flags & naca->debug_switch;

	if (active_debugs) {
		va_list ap;
		unsigned char buf[UDBG_BUFSIZE];
		unsigned long i, len = 0;

		for (i=0; i < PPCDBG_NUM_FLAGS; i++) {
			if (((1U << i) & active_debugs) && 
			    trace_names[i]) {
				len += strlen(trace_names[i]); 
				udbg_puts(trace_names[i]);
				break;
			}
		}

		snprintf(buf, UDBG_BUFSIZE, " [%s]: ", current->comm);
		len += strlen(buf); 
		udbg_puts(buf);

		while (len < 18) {
			udbg_puts(" ");
			len++;
		}

		va_start(ap, fmt);
		vsnprintf(buf, UDBG_BUFSIZE, fmt, ap);
		udbg_puts(buf);
		va_end(ap);
	}
}

unsigned long udbg_ifdebug(unsigned long flags)
{
	return (flags & naca->debug_switch);
}

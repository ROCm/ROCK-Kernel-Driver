/*
 * linux/include/asm-arm/arch-adifcc/uncompress.h
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright (c) 2001 MontaVista Software, Inc.
 *
 */

#define UART_BASE    ((volatile unsigned char *)0x00400000)

static __inline__ void putc(char c)
{
	while ((UART_BASE[5] & 0x60) != 0x60);
	UART_BASE[0] = c;
}

/*
 * This does not append a newline
 */
static void puts(const char *s)
{
	while (*s) {
		putc(*s);
		if (*s == '\n')
			putc('\r');
		s++;
	}
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()

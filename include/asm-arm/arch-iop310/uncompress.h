/*
 *  linux/include/asm-arm/arch-iop80310/uncompress.h
 */

#ifdef CONFIG_ARCH_IQ80310
#define UART1_BASE    ((volatile unsigned char *)0xfe800000)
#define UART2_BASE    ((volatile unsigned char *)0xfe810000)
#endif

static __inline__ void putc(char c)
{
	while ((UART2_BASE[5] & 0x60) != 0x60);
	UART2_BASE[0] = c;
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

/*
 *  linux/include/asm-arm/arch-iop80310/uncompress.h
 */
#include <linux/config.h>
#include <linux/serial_reg.h>
#include <asm/hardware.h>

#ifdef CONFIG_ARCH_IQ80310
#define UART2_BASE    ((volatile unsigned char *)IQ80310_UART2)
#elif defined(CONFIG_ARCH_IQ80321)
#define UART2_BASE    ((volatile unsigned char *)IQ80321_UART1)
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

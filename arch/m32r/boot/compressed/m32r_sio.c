/*
 * arch/m32r/boot/compressed/m32r_sio.c
 *
 * 2003-02-12:	Takeo Takahashi
 *
 */

#include <linux/config.h>
#include <asm/m32r.h>
#include <asm/io.h>

void putc(char c);

int puts(const char *s)
{
	char c;
	while ((c = *s++)) putc(c);
	return 0;
}

#if defined(CONFIG_PLAT_M32700UT_Alpha) || defined(CONFIG_PLAT_M32700UT)
#define USE_FPGA_MAP	0

#if USE_FPGA_MAP
/*
 * fpga configuration program uses MMU, and define map as same as
 * M32104 uT-Engine board.
 */
#define BOOT_SIO0STS	(volatile unsigned short *)(0x02c00000 + 0x20006)
#define BOOT_SIO0TXB	(volatile unsigned short *)(0x02c00000 + 0x2000c)
#else
#undef PLD_BASE
#define PLD_BASE	0xa4c00000
#define BOOT_SIO0STS	PLD_ESIO0STS
#define BOOT_SIO0TXB	PLD_ESIO0TXB
#endif

void putc(char c)
{

	while ((*BOOT_SIO0STS & 0x3) != 0x3) ;
	if (c == '\n') {
		*BOOT_SIO0TXB = '\r';
		while ((*BOOT_SIO0STS & 0x3) != 0x3) ;
	}
	*BOOT_SIO0TXB = c;
}
#else
void putc(char c)
{
	/* do nothing */
}
#endif


/* Board specific functions for those embedded 8xx boards that do
 * not have boot monitor support for board information.
 */
#include <sys/types.h>
#include <linux/config.h>
#ifdef CONFIG_8xx
#include <asm/mpc8xx.h>
#endif
#ifdef CONFIG_8260
#include <asm/mpc8260.h>
#endif


/* IIC functions.
 * These are just the basic master read/write operations so we can
 * examine serial EEPROM.
 */
extern void	iic_read(uint devaddr, u_char *buf, uint offset, uint count);
extern u_char	aschex_to_byte(u_char *cp);

/* Supply a default Ethernet address for those eval boards that don't
 * ship with one.  This is an address from the MBX board I have, so
 * it is unlikely you will find it on your network.
 */
static	ushort	def_enet_addr[] = { 0x0800, 0x3e26, 0x1559 };

#if defined(CONFIG_RPXLITE) || defined(CONFIG_RPXCLASSIC)

static void	rpx_eth(bd_t *bd, u_char *cp);
static void	rpx_brate(bd_t *bd, u_char *cp);
static void	rpx_memsize(bd_t *bd, u_char *cp);
static void	rpx_cpuspeed(bd_t *bd, u_char *cp);

/* Read the EEPROM on the RPX-Lite board.
*/
void
rpx_cfg(bd_t *bd)
{
	u_char	eebuf[256], *cp;

	/* Read the first 256 bytes of the EEPROM.  I think this
	 * is really all there is, and I hope if it gets bigger the
	 * info we want is still up front.
	 */
#if 1
	iic_read(0xa8, eebuf, 0, 128);
	iic_read(0xa8, &eebuf[128], 128, 128);
	{
		int i;
		cp = (u_char *)0xfa000000;

		for (i=0; i<256; i++)
			*cp++ = eebuf[i];
	}

	/* We look for two things, the Ethernet address and the
	 * serial baud rate.  The records are separated by
	 * newlines.
	 */
	cp = eebuf;
	for (;;) {
		if (*cp == 'E') {
			cp++;
			if (*cp == 'A') {
				cp += 2;
				rpx_eth(bd, cp);
			}
		}
		if (*cp == 'S') {
			cp++;
			if (*cp == 'B') {
				cp += 2;
				rpx_brate(bd, cp);
			}
		}
		if (*cp == 'D') {
			cp++;
			if (*cp == '1') {
				cp += 2;
				rpx_memsize(bd, cp);
			}
		}
		if (*cp == 'H') {
			cp++;
			if (*cp == 'Z') {
				cp += 2;
				rpx_cpuspeed(bd, cp);
			}
		}

		/* Scan to the end of the record.
		*/
		while ((*cp != '\n') && (*cp != 0xff))
			cp++;

		/* If the next character is a 0 or ff, we are done.
		*/
		cp++;
		if ((*cp == 0) || (*cp == 0xff))
			break;
	}
	bd->bi_memstart = 0;

#else
	/* For boards without initialized EEPROM.
	*/
	bd->bi_memstart = 0;
	bd->bi_memsize = (8 * 1024 * 1024);
	bd->bi_intfreq = 48;
	bd->bi_busfreq = 48;
	bd->bi_baudrate = 9600;
#endif
}

static void
rpx_eth(bd_t *bd, u_char *cp)
{
	int	i;

	for (i=0; i<6; i++) {
		bd->bi_enetaddr[i] = aschex_to_byte(cp);
		cp += 2;
	}
}

static void
rpx_brate(bd_t *bd, u_char *cp)
{
	uint	rate;

	rate = 0;

	while (*cp != '\n') {
		rate *= 10;
		rate += (*cp) - '0';
		cp++;
	}

	bd->bi_baudrate = rate * 100;
}

static void
rpx_memsize(bd_t *bd, u_char *cp)
{
	uint	size;

	size = 0;

	while (*cp != '\n') {
		size *= 10;
		size += (*cp) - '0';
		cp++;
	}

	bd->bi_memsize = size * 1024 * 1024;
}

static void
rpx_cpuspeed(bd_t *bd, u_char *cp)
{
	uint	num, den;

	num = den = 0;

	while (*cp != '\n') {
		num *= 10;
		num += (*cp) - '0';
		cp++;
		if (*cp == '/') {
			cp++;
			den = (*cp) - '0';
			break;
		}
	}

	/* I don't know why the RPX just can't state the actual
	 * CPU speed.....
	 */
	if (den) {
		num /= den;
		num *= den;
	}
	bd->bi_intfreq = bd->bi_busfreq = num;

	/* The 8xx can only run a maximum 50 MHz bus speed (until
	 * Motorola changes this :-).  Greater than 50 MHz parts
	 * run internal/2 for bus speed.
	 */
	if (num > 50)
		bd->bi_busfreq /= 2;
}
#endif /* RPXLITE || RPXCLASSIC */

#ifdef CONFIG_BSEIP
/* Build a board information structure for the BSE ip-Engine.
 * There is more to come since we will add some environment
 * variables and a function to read them.
 */
void
bseip_cfg(bd_t *bd)
{
	u_char	*cp;
	int	i;

	/* Baud rate and processor speed will eventually come
	 * from the environment variables.
	 */
	bd->bi_baudrate = 9600;

	/* Get the Ethernet station address from the Flash ROM.
	*/
	cp = (u_char *)0xfe003ffa;
	for (i=0; i<6; i++) {
		bd->bi_enetaddr[i] = *cp++;
	}

	/* The rest of this should come from the environment as well.
	*/
	bd->bi_memstart = 0;
	bd->bi_memsize = (16 * 1024 * 1024);
	bd->bi_intfreq = 48;
	bd->bi_busfreq = 48;
}
#endif /* BSEIP */

#ifdef CONFIG_EST8260
void
embed_config(bd_t *bd)
{
	u_char	*cp;
	int	i;

#if 0
	/* This is actually provided by my boot rom.  I have it
	 * here for those people that may load the kernel with
	 * a JTAG/COP tool and not the rom monitor.
	 */
	bd->bi_baudrate = 115200;
	bd->bi_intfreq = 200;
	bd->bi_busfreq = 66;
	bd->bi_cpmfreq = 66;
	bd->bi_brgfreq = 33;
	bd->bi_memsize = 16 * 1024 * 1024;
#endif

	cp = (u_char *)def_enet_addr;
	for (i=0; i<6; i++) {
		bd->bi_enetaddr[i] = *cp++;
	}
}
#endif /* EST8260 */


/* Board information for the SBCPowerQUICCII, which should be generic for
 * all 8260 boards.  The IMMR is now given to us so the hard define
 * will soon be removed.  All of the clock values are computed from
 * the configuration SCMR and the Power-On-Reset word.
 */

#define IMAP_ADDR			0xf0000000

#define SBC82xx_TODC_NVRAM_ADDR		0x80000000

#define SBC82xx_MACADDR_NVRAM_FCC1	0x220000c9	/* JP6B */
#define SBC82xx_MACADDR_NVRAM_SCC1	0x220000cf	/* JP6A */
#define SBC82xx_MACADDR_NVRAM_FCC2	0x220000d5	/* JP7A */
#define SBC82xx_MACADDR_NVRAM_FCC3	0x220000db	/* JP7B */


/* A Board Information structure that is given to a program when
 * prom starts it up.
 */
typedef struct bd_info {
	unsigned int	bi_memstart;	/* Memory start address */
	unsigned int	bi_memsize;	/* Memory (end) size in bytes */
	unsigned int	bi_intfreq;	/* Internal Freq, in Hz */
	unsigned int	bi_busfreq;	/* Bus Freq, in MHz */
	unsigned int	bi_cpmfreq;	/* CPM Freq, in MHz */
	unsigned int	bi_brgfreq;	/* BRG Freq, in MHz */
	unsigned int	bi_vco;		/* VCO Out from PLL */
	unsigned int	bi_baudrate;	/* Default console baud rate */
	unsigned char	bi_enetaddrs[4][6];
#define bi_enetaddr	bi_enetaddrs[0]
} bd_t;

extern bd_t m8xx_board_info;


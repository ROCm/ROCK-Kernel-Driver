/* Board information for the SBCPowerQUICCII, which should be generic for
 * all 8260 boards.  The IMMR is now given to us so the hard define
 * will soon be removed.  All of the clock values are computed from
 * the configuration SCMR and the Power-On-Reset word.
 */

#ifndef __PPC_SBC82xx_H__
#define __PPC_SBC82xx_H__

#include <asm/ppcboot.h>

#define IMAP_ADDR			0xf0000000
#define CPM_MAP_ADDR			0xf0000000

#define SBC82xx_TODC_NVRAM_ADDR		0x80000000

#define SBC82xx_MACADDR_NVRAM_FCC1	0x220000c9	/* JP6B */
#define SBC82xx_MACADDR_NVRAM_SCC1	0x220000cf	/* JP6A */
#define SBC82xx_MACADDR_NVRAM_FCC2	0x220000d5	/* JP7A */
#define SBC82xx_MACADDR_NVRAM_FCC3	0x220000db	/* JP7B */

#define BOOTROM_RESTART_ADDR      ((uint)0x40000104)

#endif /* __PPC_SBC82xx_H__ */

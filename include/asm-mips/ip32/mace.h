/*
 * Definitions for the SGI O2 Mace chip.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 */

#ifndef __ASM_MACE_H__
#define __ASM_MACE_H__

#include <asm/addrspace.h>
#include <asm/system.h>
/*
 * Address map
 */
#define MACE_BASE		KSEG1ADDR(0x1f000000)
#define MACE_PCI		(0x00080000)
#define MACE_VIN1		(0x00100000)
#define MACE_VIN2		(0x00180000)
#define MACE_VOUT		(0x00200000)
#define MACE_ENET		(0x00280000)
#define MACE_PERIF		(0x00300000)
#define MACE_ISA_EXT		(0x00380000)

#define MACE_AUDIO_BASE		(MACE_PERIF		)
#define MACE_ISA_BASE		(MACE_PERIF + 0x00010000)
#define MACE_KBDMS_BASE		(MACE_PERIF + 0x00020000)
#define MACE_I2C_BASE		(MACE_PERIF + 0x00030000)
#define MACE_UST_BASE		(MACE_PERIF + 0x00040000)


#undef BIT
#define BIT(__bit_offset) (1UL << (__bit_offset))

/*
 * Mace MACEPCI interface, 32 bit regs
 */
#define MACEPCI_ERROR_ADDR		(MACE_PCI	      )
#define MACEPCI_ERROR_FLAGS		(MACE_PCI + 0x00000004)
#define MACEPCI_CONTROL			(MACE_PCI + 0x00000008)
#define MACEPCI_REV			(MACE_PCI + 0x0000000c)
#define MACEPCI_WFLUSH			(MACE_PCI + 0x0000000c) /* ??? --IV !!! It's for flushing read buffers on PCI MEMORY accesses!!! */
#define MACEPCI_CONFIG_ADDR		(MACE_PCI + 0x00000cf8)
#define MACEPCI_CONFIG_DATA		(MACE_PCI + 0x00000cfc)
#define MACEPCI_LOW_MEMORY		0x1a000000
#define MACEPCI_LOW_IO			0x18000000
#define MACEPCI_SWAPPED_VIEW		0
#define MACEPCI_NATIVE_VIEW		0x40000000
#define MACEPCI_IO			0x80000000
/*#define MACEPCI_HI_MEMORY		0x0000000280000000UL * This mipght be just 0x0000000200000000UL 2G more :) (or maybe it is different between 1.1 & 1.5 */
#define MACEPCI_HI_MEMORY		0x0000000200000000UL /* This mipght be just 0x0000000200000000UL 2G more :) (or maybe it is different between 1.1 & 1.5 */
#define MACEPCI_HI_IO			0x0000000100000000UL

/*
 * Bits in the MACEPCI_CONTROL register
 */
#define MACEPCI_CONTROL_INT(x)		BIT(x)
#define MACEPCI_CONTROL_INT_MASK	0xff
#define MACEPCI_CONTROL_SERR_ENA	BIT(8)
#define MACEPCI_CONTROL_ARB_N6		BIT(9)
#define MACEPCI_CONTROL_PARITY_ERR	BIT(10)
#define MACEPCI_CONTROL_MRMRA_ENA	BIT(11)
#define MACEPCI_CONTROL_ARB_N3		BIT(12)
#define MACEPCI_CONTROL_ARB_N4		BIT(13)
#define MACEPCI_CONTROL_ARB_N5		BIT(14)
#define MACEPCI_CONTROL_PARK_LIU	BIT(15)
#define MACEPCI_CONTROL_INV_INT(x)	BIT(16+x)
#define MACEPCI_CONTROL_INV_INT_MASK	0x00ff0000
#define MACEPCI_CONTROL_OVERRUN_INT	BIT(24)
#define MACEPCI_CONTROL_PARITY_INT	BIT(25)
#define MACEPCI_CONTROL_SERR_INT	BIT(26)
#define MACEPCI_CONTROL_IT_INT		BIT(27)
#define MACEPCI_CONTROL_RE_INT		BIT(28)
#define MACEPCI_CONTROL_DPED_INT	BIT(29)
#define MACEPCI_CONTROL_TAR_INT		BIT(30)
#define MACEPCI_CONTROL_MAR_INT		BIT(31)

/*
 * Bits in the MACE_PCI error register
 */
#define MACEPCI_ERROR_MASTER_ABORT		BIT(31)
#define MACEPCI_ERROR_TARGET_ABORT		BIT(30)
#define MACEPCI_ERROR_DATA_PARITY_ERR		BIT(29)
#define MACEPCI_ERROR_RETRY_ERR		BIT(28)
#define MACEPCI_ERROR_ILLEGAL_CMD		BIT(27)
#define MACEPCI_ERROR_SYSTEM_ERR		BIT(26)
#define MACEPCI_ERROR_INTERRUPT_TEST		BIT(25)
#define MACEPCI_ERROR_PARITY_ERR		BIT(24)
#define MACEPCI_ERROR_OVERRUN			BIT(23)
#define MACEPCI_ERROR_RSVD			BIT(22)
#define MACEPCI_ERROR_MEMORY_ADDR		BIT(21)
#define MACEPCI_ERROR_CONFIG_ADDR		BIT(20)
#define MACEPCI_ERROR_MASTER_ABORT_ADDR_VALID	BIT(19)
#define MACEPCI_ERROR_TARGET_ABORT_ADDR_VALID	BIT(18)
#define MACEPCI_ERROR_DATA_PARITY_ADDR_VALID	BIT(17)
#define MACEPCI_ERROR_RETRY_ADDR_VALID		BIT(16)
#define MACEPCI_ERROR_SIG_TABORT		BIT(4)
#define MACEPCI_ERROR_DEVSEL_MASK		0xc0
#define MACEPCI_ERROR_DEVSEL_FAST		0
#define MACEPCI_ERROR_DEVSEL_MED		0x40
#define MACEPCI_ERROR_DEVSEL_SLOW		0x80
#define MACEPCI_ERROR_FBB			BIT(1)
#define MACEPCI_ERROR_66MHZ			BIT(0)

/*
 * Mace timer registers - 64 bit regs (63:32 are UST, 31:0 are MSC)
 */
#define MSC_PART(__reg) ((__reg) & 0x00000000ffffffff)
#define UST_PART(__reg) (((__reg) & 0xffffffff00000000) >> 32)

#define MACE_UST_UST		(MACE_UST_BASE		   ) /* Universial system time */
#define MACE_UST_COMPARE1	(MACE_UST_BASE + 0x00000008) /* Interrupt compare reg 1 */
#define MACE_UST_COMPARE2	(MACE_UST_BASE + 0x00000010) /* Interrupt compare reg 2 */
#define MACE_UST_COMPARE3	(MACE_UST_BASE + 0x00000018) /* Interrupt compare reg 3 */
#define MACE_UST_PERIOD_NS	960	/* UST Period in ns  */

#define MACE_UST_AIN_MSC	(MACE_UST_BASE + 0x00000020) /* Audio in MSC/UST pair */
#define MACE_UST_AOUT1_MSC	(MACE_UST_BASE + 0x00000028) /* Audio out 1 MSC/UST pair */
#define MACE_UST_AOUT2_MSC	(MACE_UST_BASE + 0x00000030) /* Audio out 2 MSC/UST pair */
#define MACE_VIN1_MSC_UST	(MACE_UST_BASE + 0x00000038) /* Video In 1 MSC/UST pair */
#define MACE_VIN2_MSC_UST	(MACE_UST_BASE + 0x00000040) /* Video In 2 MSC/UST pair */
#define MACE_VOUT_MSC_UST	(MACE_UST_BASE + 0x00000048) /* Video out MSC/UST pair */

/*
 * Mace "ISA" peripherals
 */
#define MACEISA_EPP_BASE   	(MACE_ISA_EXT		  )
#define MACEISA_ECP_BASE   	(MACE_ISA_EXT + 0x00008000)
#define MACEISA_SER1_BASE	(MACE_ISA_EXT + 0x00010000)
#define MACEISA_SER1_REGS       (MACE_ISA_BASE + 0x00020000)
#define MACEISA_SER2_BASE	(MACE_ISA_EXT + 0x00018000)
#define MACEISA_SER2_REGS       (MACE_ISA_BASE + 0x00030000)
#define MACEISA_RTC_BASE	(MACE_ISA_EXT + 0x00020000)
#define MACEISA_GAME_BASE	(MACE_ISA_EXT + 0x00030000)

/*
 * Ringbase address and reset register - 64 bits
 */
#define MACEISA_RINGBASE	MACE_ISA_BASE
/* Ring buffers occupy 8 4K buffers */
#define MACEISA_RINGBUFFERS_SIZE 8*4*1024

/*
 * Flash-ROM/LED/DP-RAM/NIC Controller Register - 64 bits (?)
 */
#define MACEISA_FLASH_NIC_REG	(MACE_ISA_BASE + 0x00000008)

/*
 * Bit definitions for that
 */
#define MACEISA_FLASH_WE       BIT(0) /* 1=> Enable FLASH writes */
#define MACEISA_PWD_CLEAR      BIT(1) /* 1=> PWD CLEAR jumper detected */
#define MACEISA_NIC_DEASSERT   BIT(2)
#define MACEISA_NIC_DATA       BIT(3)
#define MACEISA_LED_RED        BIT(4) /* 0=> Illuminate RED LED */
#define MACEISA_LED_GREEN      BIT(5) /* 0=> Illuminate GREEN LED */
#define MACEISA_DP_RAM_ENABLE  BIT(6)

/*
 * ISA interrupt and status registers - 32 bit
 */
#define MACEISA_INT_STAT	(MACE_ISA_BASE + 0x00000014)
#define MACEISA_INT_MASK	(MACE_ISA_BASE + 0x0000001c)

/*
 * Bits in the status/mask registers
 */
#define MACEISA_AUDIO_SW_INT		BIT (0)
#define MACEISA_AUDIO_SC_INT		BIT (1)
#define MACEISA_AUDIO1_DMAT_INT		BIT (2)
#define MACEISA_AUDIO1_OF_INT		BIT (3)
#define MACEISA_AUDIO2_DMAT_INT		BIT (4)
#define MACEISA_AUDIO2_MERR_INT		BIT (5)
#define MACEISA_AUDIO3_DMAT_INT		BIT (6)
#define MACEISA_AUDIO3_MERR_INT		BIT (7)
#define MACEISA_RTC_INT			BIT (8)
#define MACEISA_KEYB_INT		BIT (9)
#define MACEISA_KEYB_POLL_INT		BIT (10)
#define MACEISA_MOUSE_INT		BIT (11)
#define MACEISA_MOUSE_POLL_INT		BIT (12)
#define MACEISA_TIMER0_INT		BIT (13)
#define MACEISA_TIMER1_INT		BIT (14)
#define MACEISA_TIMER2_INT		BIT (15)
#define MACEISA_PARALLEL_INT		BIT (16)
#define MACEISA_PAR_CTXA_INT		BIT (17)
#define MACEISA_PAR_CTXB_INT		BIT (18)
#define MACEISA_PAR_MERR_INT		BIT (19)
#define MACEISA_SERIAL1_INT		BIT (20)
#define MACEISA_SERIAL1_TDMAT_INT	BIT (21)
#define MACEISA_SERIAL1_TDMAPR_INT	BIT (22)
#define MACEISA_SERIAL1_TDMAME_INT	BIT (23)
#define MACEISA_SERIAL1_RDMAT_INT	BIT (24)
#define MACEISA_SERIAL1_RDMAOR_INT	BIT (25)
#define MACEISA_SERIAL2_INT		BIT (26)
#define MACEISA_SERIAL2_TDMAT_INT	BIT (27)
#define MACEISA_SERIAL2_TDMAPR_INT	BIT (28)
#define MACEISA_SERIAL2_TDMAME_INT	BIT (29)
#define MACEISA_SERIAL2_RDMAT_INT	BIT (30)
#define MACEISA_SERIAL2_RDMAOR_INT	BIT (31)

#define MACEI2C_CONFIG	MACE_I2C_BASE
#define MACEI2C_CONTROL	(MACE_I2C_BASE|0x10)
#define MACEI2C_DATA	(MACE_I2C_BASE|0x18)

/* Bits for I2C_CONFIG */
#define MACEI2C_RESET           BIT(0)
#define MACEI2C_FAST            BIT(1)
#define MACEI2C_DATA_OVERRIDE   BIT(2)
#define MACEI2C_CLOCK_OVERRIDE  BIT(3)
#define MACEI2C_DATA_STATUS     BIT(4)
#define MACEI2C_CLOCK_STATUS    BIT(5)

/* Bits for I2C_CONTROL */
#define MACEI2C_NOT_IDLE        BIT(0)	/* write: 0=force idle
				         * read: 0=idle 1=not idle */
#define MACEI2C_DIR		BIT(1)	/* 0=write 1=read */
#define MACEI2C_MORE_BYTES	BIT(2)	/* 0=last byte 1=more bytes */
#define MACEI2C_TRANS_BUSY	BIT(4)	/* 0=trans done 1=trans busy */
#define MACEI2C_NACK	        BIT(5)	/* 0=ack received 1=ack not */
#define MACEI2C_BUS_ERROR	BIT(7)	/* 0=no bus err 1=bus err */


#define MACEISA_AUDIO_INT (MACEISA_AUDIO_SW_INT |               \
                           MACEISA_AUDIO_SC_INT |               \
                           MACEISA_AUDIO1_DMAT_INT |            \
                           MACEISA_AUDIO1_OF_INT |              \
                           MACEISA_AUDIO2_DMAT_INT |            \
                           MACEISA_AUDIO2_MERR_INT |            \
                           MACEISA_AUDIO3_DMAT_INT |            \
                           MACEISA_AUDIO3_MERR_INT)
#define MACEISA_MISC_INT (MACEISA_RTC_INT |                     \
                          MACEISA_KEYB_INT |                    \
                          MACEISA_KEYB_POLL_INT |               \
                          MACEISA_MOUSE_INT |                   \
                          MACEISA_MOUSE_POLL_INT |              \
                          MACEISA_TIMER0_INT |                  \
                          MACEISA_TIMER1_INT |                  \
                          MACEISA_TIMER2_INT)
#define MACEISA_SUPERIO_INT (MACEISA_PARALLEL_INT |             \
                             MACEISA_PAR_CTXA_INT |             \
                             MACEISA_PAR_CTXB_INT |             \
                             MACEISA_PAR_MERR_INT |             \
                             MACEISA_SERIAL1_INT |              \
                             MACEISA_SERIAL1_TDMAT_INT |        \
                             MACEISA_SERIAL1_TDMAPR_INT |       \
                             MACEISA_SERIAL1_TDMAME_INT |       \
                             MACEISA_SERIAL1_RDMAT_INT |        \
                             MACEISA_SERIAL1_RDMAOR_INT |       \
                             MACEISA_SERIAL2_INT |              \
                             MACEISA_SERIAL2_TDMAT_INT |        \
                             MACEISA_SERIAL2_TDMAPR_INT |       \
                             MACEISA_SERIAL2_TDMAME_INT |       \
                             MACEISA_SERIAL2_RDMAT_INT |        \
                             MACEISA_SERIAL2_RDMAOR_INT)

#ifndef __ASSEMBLY__
#include <asm/types.h>

/*
 * XXX Some of these are probably not needed (or even legal?)
 */
static inline u8 mace_read_8 (unsigned long __offset)
{
	return *((volatile u8 *) (MACE_BASE + __offset));
}

static inline u16 mace_read_16 (unsigned long __offset)
{
	return *((volatile u16 *) (MACE_BASE + __offset));
}

static inline u32 mace_read_32 (unsigned long __offset)
{
	return *((volatile u32 *) (MACE_BASE + __offset));
}

static inline u64 mace_read_64 (unsigned long __offset)
{
	return *((volatile u64 *) (MACE_BASE + __offset));
}

static inline void mace_write_8 (unsigned long __offset, u8 __val)
{
	*((volatile u8 *) (MACE_BASE + __offset)) = __val;
}

static inline void mace_write_16 (unsigned long __offset, u16 __val)
{
	*((volatile u16 *) (MACE_BASE + __offset)) = __val;
}

static inline void mace_write_32 (unsigned long __offset, u32 __val)
{
	*((volatile u32 *) (MACE_BASE + __offset)) = __val;
}

static inline void mace_write_64 (unsigned long __offset, u64 __val)
{
	*((volatile u64 *) (MACE_BASE + __offset)) = __val;
}

/* Call it whenever device needs to read data from main memory coherently */
static inline void mace_inv_read_buffers(void)
{
/*	mace_write_32(MACEPCI_WFLUSH,0xffffffff);*/
}
#endif /* !__ASSEMBLY__ */


#endif /* __ASM_MACE_H__ */

/*
 *  linux/include/asm-mips/tx3912.h
 *
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Register includes for TMPR3912/05 and PR31700 processors
 */
#ifndef __TX3912_H__
#define __TX3912_H__

#include <asm/addrspace.h>

/******************************************************************************
*
* 	01  General macro definitions
*
******************************************************************************/

#define REGISTER_BASE   0xb0c00000

#ifndef _LANGUAGE_ASSEMBLY

	#define REG_AT(x)	(*((volatile unsigned long *)(REGISTER_BASE + x)))

#else

	#define REG_AT(x)   (REGISTER_BASE + x)

#endif

#define BIT(x)	(1 << x)

/******************************************************************************
*
* 	02  Bus Interface Unit
*
******************************************************************************/

#define MemConfig0    REG_AT(0x000)
#define MemConfig1    REG_AT(0x004)
#define MemConfig2    REG_AT(0x008)
#define MemConfig3    REG_AT(0x00c)
#define MemConfig4    REG_AT(0x010)
#define MemConfig5    REG_AT(0x014)
#define MemConfig6    REG_AT(0x018)
#define MemConfig7    REG_AT(0x01c)
#define MemConfig8    REG_AT(0x020)

/* Memory config register 1 */
#define MEM1_ENCS1USER	BIT(21)

/* Memory config register 3 */
#define MEM3_CARD1ACCVAL_MASK	(BIT(24) | BIT(25) | BIT(26) | BIT(27))
#define MEM3_CARD1IOEN		BIT(4)

/* Memory config register 4 */
#define MEM4_ARBITRATIONEN	BIT(29)
#define MEM4_MEMPOWERDOWN	BIT(16)
#define MEM4_ENREFRESH1		BIT(15)
#define MEM4_ENREFRESH0		BIT(14)
#define MEM4_ENWATCH            BIT(24)
#define MEM4_WATCHTIMEVAL_MASK  (0xf)
#define MEM4_WATCHTIMEVAL_SHIFT (20)
#define MEM4_WATCHTIME_VALUE    (0xf)

/*
 ***********************************************************************
 *								       *
 * 06  Clock Module						       *
 *								       *
 ***********************************************************************
 */
#define TX3912_CLK_CTRL_BASE			(REGISTER_BASE + 0x1c0)

#define TX3912_CLK_CTRL_CHICLKDIV_MASK		0xff000000
#define TX3912_CLK_CTRL_CHICLKDIV_SHIFT		24
#define TX3912_CLK_CTRL_ENCLKTEST		0x00800000
#define TX3912_CLK_CTRL_CLKTESTSELSIB		0x00400000
#define TX3912_CLK_CTRL_CHIMCLKSEL		0x00200000
#define TX3912_CLK_CTRL_CHICLKDIR		0x00100000
#define TX3912_CLK_CTRL_ENCHIMCLK		0x00080000
#define TX3912_CLK_CTRL_ENVIDCLK		0x00040000
#define TX3912_CLK_CTRL_ENMBUSCLK		0x00020000
#define TX3912_CLK_CTRL_ENSPICLK		0x00010000
#define TX3912_CLK_CTRL_ENTIMERCLK		0x00008000
#define TX3912_CLK_CTRL_ENFASTTIMERCLK		0x00004000
#define TX3912_CLK_CTRL_SIBMCLKDIR		0x00002000
#define TX3912_CLK_CTRL_RESERVED		0x00001000
#define TX3912_CLK_CTRL_ENSIBMCLK		0x00000800
#define TX3912_CLK_CTRL_SIBMCLKDIV_MASK		0x00000700
#define TX3912_CLK_CTRL_SIBMCLKDIV_SHIFT	8
#define TX3912_CLK_CTRL_CSERSEL			0x00000080
#define TX3912_CLK_CTRL_CSERDIV_MASK		0x00000070
#define TX3912_CLK_CTRL_CSERDIV_SHIFT		4
#define TX3912_CLK_CTRL_ENCSERCLK		0x00000008
#define TX3912_CLK_CTRL_ENIRCLK			0x00000004
#define TX3912_CLK_CTRL_ENUARTACLK		0x00000002
#define TX3912_CLK_CTRL_ENUARTBCLK		0x00000001




/******************************************************************************
*
* 	07  CHI module
*
******************************************************************************/

#define CHIControl		REG_AT(0x1D8)
#define CHIPointerEnable	REG_AT(0x1DC)
#define CHIReceivePtrA		REG_AT(0x1E0)
#define CHIReceivePtrB		REG_AT(0x1E4)
#define CHITransmitPtrA		REG_AT(0x1E8)
#define CHITransmitPtrB		REG_AT(0x1EC)
#define CHISize			REG_AT(0x1F0)
#define CHIReceiveStart		REG_AT(0x1F4)
#define CHITransmitStart	REG_AT(0x1F8)
#define CHIHoldingReg		REG_AT(0x1FC)

/* CHI Control Register */
/* <incomplete!> */
#define	CHI_RXEN		BIT(2)
#define	CHI_TXEN		BIT(1)
#define	CHI_ENCHI		BIT(0)

/******************************************************************************
*
*	08  Interrupt module
*
******************************************************************************/

/* Register locations */

#define IntStatus1    REG_AT(0x100)
#define IntStatus2    REG_AT(0x104)
#define IntStatus3    REG_AT(0x108)
#define IntStatus4    REG_AT(0x10c)
#define IntStatus5    REG_AT(0x110)
#define IntStatus6    REG_AT(0x114)

#define IntClear1     REG_AT(0x100)
#define IntClear2     REG_AT(0x104)
#define IntClear3     REG_AT(0x108)
#define IntClear4     REG_AT(0x10c)
#define IntClear5     REG_AT(0x110)
#define IntClear6     REG_AT(0x114)

#define IntEnable1    REG_AT(0x118)
#define IntEnable2    REG_AT(0x11c)
#define IntEnable3    REG_AT(0x120)
#define IntEnable4    REG_AT(0x124)
#define IntEnable5    REG_AT(0x128)
#define IntEnable6    REG_AT(0x12c)

/* Interrupt Status Register 1 at offset 100 */
#define INT1_LCDINT		BIT(31)
#define INT1_DFINT              BIT(30)
#define INT1_CHIDMAHALF		BIT(29)
#define INT1_CHIDMAFULL		BIT(28)
#define INT1_CHIDMACNTINT       BIT(27)
#define INT1_CHIRXAINT		BIT(26)
#define INT1_CHIRXBINT		BIT(25)
#define INT1_CHIACTINT          BIT(24)
#define INT1_CHIERRINT          BIT(23)
#define INT1_SND0_5INT          BIT(22)
#define INT1_SND1_0INT		BIT(21)
#define INT1_TEL0_5INT          BIT(20)
#define INT1_TEL1_0INT          BIT(19)
#define INT1_SNDDMACNTINT       BIT(18)
#define INT1_TELDMACNTINT       BIT(17)
#define INT1_LSNDCLIPINT        BIT(16)
#define INT1_RSNDCLIPINT        BIT(15)
#define INT1_VALSNDPOSINT       BIT(14)
#define INT1_VALSNDNEGINT       BIT(13)
#define INT1_VALTELPOSINT       BIT(12)
#define INT1_VALTELNEGINT       BIT(11)
#define INT1_SNDININT           BIT(10)
#define INT1_TELININT           BIT(9)
#define INT1_SIBSF0INT          BIT(8)
#define INT1_SIBSF1INT          BIT(7)
#define INT1_SIBIRQPOSINT       BIT(6)
#define INT1_SIBIRQNEGINT       BIT(5)

/* Interrupt Status Register 2 at offset 104 */
#define INT2_UARTARXINT		BIT(31)
#define INT2_UARTARXOVERRUN	BIT(30)
#define INT2_UARTAFRAMEINT	BIT(29)
#define INT2_UARTABREAKINT	BIT(28)
#define INT2_UARTATXINT		BIT(26)
#define INT2_UARTATXOVERRUN	BIT(25)
#define INT2_UARTAEMPTY		BIT(24)

#define INT2_UARTBRXINT		BIT(21)
#define INT2_UARTBRXOVERRUN	BIT(20)
#define INT2_UARTBFRAMEINT	BIT(29)
#define INT2_UARTBBREAKINT	BIT(18)
#define INT2_UARTBTXINT		BIT(16)
#define INT2_UARTBTXOVERRUN	BIT(15)
#define INT2_UARTBEMPTY		BIT(14)

#define INT2_UARTA_RX		(BIT(31) | BIT(30) | BIT(29) | BIT(28) | BIT(27))
#define INT2_UARTA_TX		(BIT(26) | BIT(25) | BIT(24))
#define INT2_UARTA_DMA		(BIT(23) | BIT(22))

#define INT2_UARTB_RX		(BIT(21) | BIT(20) | BIT(19) | BIT(18) | BIT(17))
#define INT2_UARTB_TX		(BIT(16) | BIT(15) | BIT(14))
#define INT2_UARTB_DMA		(BIT(13) | BIT(12))

/* Interrupt Status Register 5 */
#define INT5_RTCINT	 BIT(31)
#define INT5_ALARMINT	 BIT(30)
#define INT5_PERIODICINT BIT(29)
#define INT5_POSPWRINT 	 BIT(27)
#define INT5_NEGPWRINT	 BIT(26)
#define INT5_POSPWROKINT BIT(25)
#define INT5_NEGPWROKINT BIT(24)
#define INT5_POSONBUTINT BIT(23)
#define INT5_NEGONBUTINT BIT(22)
#define INT5_SPIAVAILINT BIT(21)        /* 0x0020 0000 */
#define INT5_SPIERRINT   BIT(20)        /* 0x0010 0000 */
#define INT5_SPIRCVINT	 BIT(19)	/* 0x0008 0000 */
#define INT5_SPIEMPTYINT BIT(18)	/* 0x0004 0000 */
#define INT5_IOPOSINT6	 BIT(13)
#define INT5_IOPOSINT5	 BIT(12)
#define INT5_IOPOSINT4	 BIT(11)
#define INT5_IOPOSINT3	 BIT(10)
#define INT5_IOPOSINT2	 BIT(9)
#define INT5_IOPOSINT1	 BIT(8)
#define INT5_IOPOSINT0	 BIT(7)
#define INT5_IONEGINT6	 BIT(6)
#define INT5_IONEGINT5	 BIT(5)
#define INT5_IONEGINT4	 BIT(4)
#define INT5_IONEGINT3	 BIT(3)
#define INT5_IONEGINT2	 BIT(2)
#define INT5_IONEGINT1	 BIT(1)
#define INT5_IONEGINT0	 BIT(0)

#define INT5_IONEGINT_SHIFT 0
#define	INT5_IONEGINT_MASK  (0x7F<<INT5_IONEGINT_SHIFT)
#define INT5_IOPOSINT_SHIFT 7
#define	INT5_IOPOSINT_MASK  (0x7F<<INT5_IOPOSINT_SHIFT)

/* Interrupt Status Register 6 */
#define INT6_IRQHIGH	BIT(31)
#define INT6_IRQLOW	BIT(30)
#define INT6_INTVECT	(BIT(5) | BIT(4) | BIT(3) | BIT(2))


/* Interrupt Enable Register 6 */
#define INT6_GLOBALEN		BIT(18)
#define INT6_PWROKINT		BIT(15)
#define	INT6_ALARMINT		BIT(14)
#define INT6_PERIODICINT 	BIT(13)
#define	INT6_MBUSINT		BIT(12)
#define INT6_UARTARXINT		BIT(11)
#define INT6_UARTBRXINT		BIT(10)
#define	INT6_MFIOPOSINT1619	BIT(9)
#define INT6_IOPOSINT56         BIT(8)
#define	INT6_MFIONEGINT1619	BIT(7)
#define INT6_IONEGINT56         BIT(6)
#define	INT6_MBUSDMAFULLINT	BIT(5)
#define	INT6_SNDDMACNTINT	BIT(4)
#define	INT6_TELDMACNTINT	BIT(3)
#define	INT6_CHIDMACNTINT	BIT(2)
#define INT6_IOPOSNEGINT0       BIT(1)

/******************************************************************************
*
*	09  GPIO and MFIO modules
*
******************************************************************************/

#define IOControl   	REG_AT(0x180)
#define MFIOOutput   	REG_AT(0x184)
#define MFIODirection  	REG_AT(0x188)
#define MFIOInput  	REG_AT(0x18c)
#define MFIOSelect   	REG_AT(0x190)
#define IOPowerDown   	REG_AT(0x194)
#define MFIOPowerDown  	REG_AT(0x198)

#define IODIN_MASK      0x0000007f
#define IODIN_SHIFT     0
#define IODOUT_MASK     0x00007f00
#define IODOUT_SHIFT    8
#define IODIREC_MASK    0x007f0000
#define IODIREC_SHIFT   16
#define IODEBSEL_MASK   0x7f000000
#define IODEBSEL_SHIFT  24

/******************************************************************************
*
*	10  IR module
*
******************************************************************************/

#define IRControl1                  REG_AT(0x0a0)
#define IRControl2                  REG_AT(0x0a4)

/* IR Control 1 Register */
#define IR_CARDRET                  BIT(24)
#define IR_BAUDVAL_MASK             0x00ff0000
#define IR_BAUDVAL_SHIFT            16
#define IR_TESTIR                   BIT(4)
#define IR_DTINVERT                 BIT(3)
#define IR_RXPWR                    BIT(2)
#define IR_ENSTATE                  BIT(1)
#define IR_ENCONSM                  BIT(0)

/* IR Control 2 Register */
#define IR_PER_MASK                 0xff000000
#define IR_PER_SHIFT                24
#define IR_ONTIME_MASK              0x00ff0000
#define IR_ONTIME_SHIFT             16
#define IR_DELAYVAL_MASK            0x0000ff00
#define IR_DELAYVAL_SHIFT           8
#define IR_WAITVAL_MASK             0x000000ff
#define IR_WAITVAL_SHIFT            0

/******************************************************************************
*
*	11  Magicbus Module
*
******************************************************************************/

#define MbusCntrl1		REG_AT(0x0e0)
#define MbusCntrl2		REG_AT(0x0e4)
#define MbusDMACntrl1		REG_AT(0x0e8)
#define MbusDMACntrl2		REG_AT(0x0ec)
#define MbusDMACount		REG_AT(0x0f0)
#define MbusTxReg		REG_AT(0x0f4)
#define MbusRxReg		REG_AT(0x0f8)

#define	MBUS_CLKPOL		BIT(4)
#define	MBUS_SLAVE		BIT(3)
#define	MBUS_FSLAVE		BIT(2)
#define	MBUS_LONG		BIT(1)
#define	MBUS_ENMBUS		BIT(0)

/******************************************************************************
*
*	12  Power module
*
******************************************************************************/

#define PowerControl   	            REG_AT(0x1C4)

#define PWR_ONBUTN                  BIT(31)
#define PWR_PWRINT                  BIT(30)
#define PWR_PWROK                   BIT(29)
#define PWR_VIDRF_MASK              (BIT(28) | BIT(27))
#define PWR_VIDRF_SHIFT             27
#define PWR_SLOWBUS                 BIT(26)
#define PWR_DIVMOD                  BIT(25)
#define PWR_STPTIMERVAL_MASK        (BIT(15) | BIT(14) | BIT(13) | BIT(12))
#define PWR_STPTIMERVAL_SHIFT       12
#define PWR_ENSTPTIMER              BIT(11)
#define PWR_ENFORCESHUTDWN          BIT(10)
#define PWR_FORCESHUTDWN            BIT(9)
#define PWR_FORCESHUTDWNOCC         BIT(8)
#define PWR_SELC2MS                 BIT(7)
#define PWR_BPDBVCC3                BIT(5)
#define PWR_STOPCPU                 BIT(4)
#define PWR_DBNCONBUTN              BIT(3)
#define PWR_COLDSTART               BIT(2)
#define PWR_PWRCS                   BIT(1)
#define PWR_VCCON                   BIT(0)

/******************************************************************************
*
*	13  SIB (Serial Interconnect Bus) Module
*
******************************************************************************/

/* Register locations */
#define SIBSize	      	        REG_AT(0x060)
#define SIBSoundRXStart	      	REG_AT(0x064)
#define SIBSoundTXStart         REG_AT(0x068)
#define SIBTelecomRXStart       REG_AT(0x06C)
#define SIBTelecomTXStart       REG_AT(0x070)
#define SIBControl              REG_AT(0x074)
#define SIBSoundTXRXHolding     REG_AT(0x078)
#define SIBTelecomTXRXHolding   REG_AT(0x07C)
#define SIBSubFrame0Control     REG_AT(0x080)
#define SIBSubFrame1Control     REG_AT(0x084)
#define SIBSubFrame0Status      REG_AT(0x088)
#define SIBSubFrame1Status      REG_AT(0x08C)
#define SIBDMAControl           REG_AT(0x090)

/* SIB Size Register */
#define SIB_SNDSIZE_MASK        0x3ffc0000
#define SIB_SNDSIZE_SHIFT       18
#define SIB_TELSIZE_MASK        0x00003ffc
#define SIB_TELSIZE_SHIFT       2

/* SIB Control Register */
#define SIB_SIBIRQ              BIT(31)
#define SIB_ENCNTTEST           BIT(30)
#define SIB_ENDMATEST           BIT(29)
#define SIB_SNDMONO             BIT(28)
#define SIB_RMONOSNDIN          BIT(27)
#define SIB_SIBSCLKDIV_MASK     (BIT(26) | BIT(25) | BIT(24))
#define SIB_SIBSCLKDIV_SHIFT    24
#define SIB_TEL16               BIT(23)
#define SIB_TELFSDIV_MASK       0x007f0000
#define SIB_TELFSDIV_SHIFT      16
#define SIB_SND16               BIT(15)
#define SIB_SNDFSDIV_MASK       0x00007f00
#define SIB_SNDFSDIV_SHIFT      8
#define SIB_SELTELSF1           BIT(7)
#define SIB_SELSNDSF1           BIT(6)
#define SIB_ENTEL               BIT(5)
#define SIB_ENSND               BIT(4)
#define SIB_SIBLOOP             BIT(3)
#define SIB_ENSF1               BIT(2)
#define SIB_ENSF0               BIT(1)
#define SIB_ENSIB               BIT(0)

/* SIB Frame Format (SIBSubFrame0Status and SIBSubFrame1Status) */
#define SIB_REGISTER_EXT        BIT(31)  /* Must be zero */
#define SIB_ADDRESS_MASK        0x78000000
#define SIB_ADDRESS_SHIFT       27
#define SIB_WRITE               BIT(26)
#define SIB_AUD_VALID           BIT(17)
#define SIB_TEL_VALID           BIT(16)
#define SIB_DATA_MASK           0x00ff
#define SIB_DATA_SHIFT          0

/* SIB DMA Control Register */
#define SIB_SNDBUFF1TIME        BIT(31)
#define SIB_SNDDMALOOP          BIT(30)
#define SIB_SNDDMAPTR_MASK      0x3ffc0000
#define SIB_SNDDMAPTR_SHIFT     18
#define SIB_ENDMARXSND          BIT(17)
#define SIB_ENDMATXSND          BIT(16)
#define SIB_TELBUFF1TIME        BIT(15)
#define SIB_TELDMALOOP          BIT(14)
#define SIB_TELDMAPTR_MASK      0x00003ffc
#define SIB_TELDMAPTR_SHIFT     2
#define SIB_ENDMARXTEL          BIT(1)
#define SIB_ENDMATXTEL          BIT(0)

/******************************************************************************
*
* 	14  SPI module
*
******************************************************************************/

#define SPIControl		REG_AT(0x160)
#define SPITransmit		REG_AT(0x164)
#define SPIReceive		REG_AT(0x164)

#define SPI_SPION		BIT(17)
#define SPI_EMPTY		BIT(16)
#define SPI_DELAYVAL_MASK	(BIT(12) | BIT(13) | BIT(14) | BIT(15))
#define SPI_DELAYVAL_SHIFT	12
#define	SPI_BAUDRATE_MASK	(BIT(8) | BIT(9) | BIT(10) | BIT(11))
#define	SPI_BAUDRATE_SHIFT	8
#define	SPI_PHAPOL		BIT(5)
#define	SPI_CLKPOL		BIT(4)
#define	SPI_WORD		BIT(2)
#define	SPI_LSB			BIT(1)
#define	SPI_ENSPI		BIT(0)

/******************************************************************************
*
*	15  Timer module
*
******************************************************************************/

#define RTChigh	      	REG_AT(0x140)
#define RTClow	        REG_AT(0x144)
#define RTCalarmHigh    REG_AT(0x148)
#define RTCalarmLow     REG_AT(0x14c)
#define RTCtimerControl REG_AT(0x150)
#define RTCperiodTimer  REG_AT(0x154)

/* RTC Timer Control */
#define TIM_FREEZEPRE	BIT(7)
#define TIM_FREEZERTC	BIT(6)
#define TIM_FREEZETIMER	BIT(5)
#define TIM_ENPERTIMER	BIT(4)
#define TIM_RTCCLEAR	BIT(3)

#define	RTC_HIGHMASK	(0xFF)

/* RTC Periodic Timer */
#define	TIM_PERCNT	0xFFFF0000
#define	TIM_PERVAL	0x0000FFFF

/* For a system clock frequency of 36.864MHz, the timer counts at one tick
   every 868nS (ie CLK/32). Therefore 11520 counts gives a 10mS interval
 */
#define PER_TIMER_COUNT (1152000/HZ)

/*
 ***********************************************************************
 *								       *
 * 15  UART Module						       *
 *								       *
 ***********************************************************************
 */
#define TX3912_UARTA_BASE       (REGISTER_BASE + 0x0b0)
#define TX3912_UARTB_BASE       (REGISTER_BASE + 0x0c8)

/*
 * TX3912 UART register offsets
 */
#define TX3912_UART_CTRL1       0x00
#define TX3912_UART_CTRL2       0x04
#define TX3912_UART_DMA_CTRL1   0x08
#define TX3912_UART_DMA_CTRL2   0x0c
#define TX3912_UART_DMA_CNT     0x10
#define TX3912_UART_DATA        0x14

#define UartA_Ctrl1   REG_AT(0x0b0)
#define UartA_Data    REG_AT(0x0c4)

/*
 * Defines for UART Control Register 1
 */
#define TX3912_UART_CTRL1_UARTON	0x80000000
#define UART_TX_EMPTY		BIT(30)
#define UART_PRX_HOLD_FULL	BIT(29)
#define UART_RX_HOLD_FULL	BIT(28)
#define UART_EN_DMA_RX 		BIT(15)
#define UART_EN_DMA_TX 		BIT(14)
#define UART_BREAK_HALT 	BIT(12)
#define UART_DMA_LOOP		BIT(10)
#define UART_PULSE_THREE	BIT(9)
#define UART_PULSE_SIX		BIT(8)
#define UART_DT_INVERT		BIT(7)
#define UART_DIS_TXD		BIT(6)
#define UART_LOOPBACK		BIT(4)
#define TX3912_UART_CTRL1_ENUART	0x00000001
		 
#define SER_SEVEN_BIT		BIT(3)
#define SER_EIGHT_BIT		    0
#define SER_EVEN_PARITY 	(BIT(2) | BIT(1))
#define SER_ODD_PARITY  	BIT(1)
#define SER_NO_PARITY		    0
#define SER_TWO_STOP		BIT(5)
#define SER_ONE_STOP		    0

/*
 * Defines for UART Control Register 2
 *
 *              3.6864MHz
 * divisors =  ----------- - 1
 *             (baud * 16)
 */
#define TX3912_UART_CTRL2_B230400	0x000	/*   0 */
#define TX3912_UART_CTRL2_B115200	0x001	/*   1 */
#define TX3912_UART_CTRL2_B76800	0x002	/*   2 */
#define TX3912_UART_CTRL2_B57600	0x003	/*   3 */
#define TX3912_UART_CTRL2_B38400	0x005	/*   5 */
#define TX3912_UART_CTRL2_B19200	0x00b	/*  11 */
#define TX3912_UART_CTRL2_B9600		0x016	/*  22 */
#define TX3912_UART_CTRL2_B4800		0x02f	/*  47 */
#define TX3912_UART_CTRL2_B2400		0x05f	/*  95 */
#define TX3912_UART_CTRL2_B1200		0x0bf	/* 191 */
#define TX3912_UART_CTRL2_B600		0x17f	/* 383 */
#define TX3912_UART_CTRL2_B300		0x2ff	/* 767 */

#endif	/* __TX3912_H__ */

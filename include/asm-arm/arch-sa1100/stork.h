/*
	stork.h

*/

#ifndef STORK_SA1100_H
#define STORK_SA1100_H

/* ugly - this will make sure we build sa1100fb for the Nec display not the Kyocera */
#define STORK_TFT	1


#define GPIO_STORK_SWITCH_1		(1 << 0)    /* Switch 1 - input */
#define GPIO_STORK_SWITCH_2		(1 << 1)    /* Switch 2 - input */
#define GPIO_STORK_TOUCH_SCREEN_BUSY	(1 << 10)   /* TOUCH_SCREEN_BUSY - input */
#define GPIO_STORK_TOUCH_SCREEN_DATA	(1 << 11)   /* TOUCH_SCREEN_DATA - input */
#define GPIO_STORK_CODEC_AGCSTAT	(1 << 12)   /* CODEC_AGCSTAT -input */
#define GPIO_STORK_RS232_ON		(1 << 13)   /* enable RS232 (UART1) */
#define GPIO_STORK_TEST_POINT		(1 << 14)   /* to test point */
#define GPIO_STORK_L3_I2C_SDA		(1 << 15)   /* L3_I2C_SDA - bidirectional */
#define GPIO_STORK_PSU_SYNC_MODE	(1 << 16)   /* PSU_SYNC_MODE - output */
#define GPIO_STORK_L3_MODE		(1 << 17)   /* L3 mode - output (??) */
#define GPIO_STORK_L3_I2C_SCL		(1 << 18)   /* L3_I2C_SCL - bidirectional */
#define GPIO_STORK_AUDIO_CLK		(1 << 19)   /* SSP external clock (Audio clock) - input */
#define GPIO_STORK_PCMCIA_A_CARD_DETECT	(1 << 20)   /* PCMCIA_A_CARD_DETECT - input */
#define GPIO_STORK_PCMCIA_B_CARD_DETECT	(1 << 21)   /* PCMCIA_B_CARD_DETECT - input */
#define GPIO_STORK_PCMCIA_A_RDY		(1 << 22)   /* PCMCIA_A_RDY - input */
#define GPIO_STORK_PCMCIA_B_RDY		(1 << 23)   /* PCMCIA_B_RDY - input */
#define GPIO_STORK_SWITCH_EXTRA_IRQ	(1 << 24)   /* Extra IRQ from switch detect logic - input  */
#define GPIO_STORK_SWITCH_IRQ		(1 << 25)   /* Sitch irq - input */
#define GPIO_STORK_BATTERY_LOW_IRQ	(1 << 26)   /* BATTERY_LOW_IRQ - input */
#define GPIO_STORK_TOUCH_SCREEN_PEN_IRQ	(1 << 27)   /* TOUCH_SCREEN_PEN_IRQ -input */

#define IRQ_GPIO_STORK_PCMCIA_A_CARD_DETECT	IRQ_GPIO20   /* PCMCIA_A_CARD_DETECT - input */
#define IRQ_GPIO_STORK_PCMCIA_B_CARD_DETECT	IRQ_GPIO21   /* PCMCIA_B_CARD_DETECT - input */

#define IRQ_GPIO_STORK_SWITCH_1			IRQ_GPIO0    /* Switch 1 - input - active low */
#define IRQ_GPIO_STORK_SWITCH_2			IRQ_GPIO1    /* Switch 2 - input - active low */
#define IRQ_GPIO_STORK_SWITCH_EXTRA_IRQ		IRQ_GPIO24   /* Extra IRQ from switch detect logic - input - active low  */
#define IRQ_GPIO_STORK_SWITCH_IRQ		IRQ_GPIO25   /* Switch irq - input- active low  */
#define IRQ_GPIO_STORK_BATTERY_LOW_IRQ		IRQ_GPIO26   /* BATTERY_LOW_IRQ - input - active low */
#define IRQ_GPIO_STORK_TOUCH_SCREEN_PEN_IRQ	IRQ_GPIO27   /* TOUCH_SCREEN_PEN_IRQ -input - goes low when it happens */

/* this may be bogus - no it's not the RDY line becomes the IRQ line when we're up as an IO device */
#define IRQ_GPIO_STORK_PCMCIA_A_RDY		IRQ_GPIO22   /* PCMCIA_A_RDY - input */
#define IRQ_GPIO_STORK_PCMCIA_B_RDY		IRQ_GPIO23   /* PCMCIA_B_RDY - input */

/* the default outputs, others are optional - I'll set these in the bootldr for now */
#define GPIO_STORK_OUTPUT_BITS (GPIO_STORK_RS232_ON | GPIO_STORK_PSU_SYNC_MODE | GPIO_STORK_L3_MODE)

#define STORK_LATCH_A_ADDR		0x08000000  /* cs1 A5 = 0 */
#define STORK_LATCH_B_ADDR		0x08000020  /* cs1 A5 = 1 */

#define STORK_LCDCPLD_BASE_ADDR		0x10000000  /* cs2 A5 = 0 */

/* bit defs for latch A - these are write only and will need to be mirrored!  */

#define STORK_TEMP_IC_POWER_ON		(1 << 0)
#define STORK_SED1386_POWER_ON		(1 << 1)
#define STORK_LCD_3V3_POWER_ON		(1 << 2)
#define STORK_LCD_5V_POWER_ON		(1 << 3)
#define STORK_LCD_BACKLIGHT_INVERTER_ON	(1 << 4)
#define STORK_PCMCIA_PULL_UPS_POWER_ON	(1 << 5)
#define STORK_PCMCIA_A_POWER_ON		(1 << 6)
#define STORK_PCMCIA_B_POWER_ON		(1 << 7)
#define STORK_AUDIO_POWER_ON		(1 << 8)
#define STORK_AUDIO_AMP_ON		(1 << 9)
#define STORK_BAR_CODE_POWER_ON		(1 << 10)
#define STORK_BATTERY_CHARGER_ON	(1 << 11)
#define STORK_SED1386_RESET		(1 << 12)
#define STORK_IRDA_FREQUENCY_SELECT	(1 << 13)
#define STORK_IRDA_MODE_0		(1 << 14)
#define STORK_IRDA_MODE_1		(1 << 15)

/* and for B */

#define STORK_AUX_AD_SEL_0		(1 << 0)
#define STORK_AUX_AD_SEL_1		(1 << 1)
#define STORK_TOUCH_SCREEN_DCLK		(1 << 2)
#define STORK_TOUCH_SCREEN_DIN		(1 << 3)
#define STORK_TOUCH_SCREEN_CS		(1 << 4)
#define STORK_DA_CS			(1 << 5)
#define STORK_DA_LD			(1 << 6)
#define STORK_RED_LED			(1 << 7)	/* active LOW */
#define STORK_GREEN_LED			(1 << 8)	/* active LOW */
#define STORK_YELLOW_LED		(1 << 9)	/* active LOW */
#define STORK_PCMCIA_B_RESET		(1 << 10)
#define STORK_PCMCIA_A_RESET		(1 << 11)
#define STORK_AUDIO_CODEC_RESET		(1 << 12)
#define STORK_CODEC_QMUTE		(1 << 13)
#define STORK_AUDIO_CLOCK_SEL0		(1 << 14)
#define STORK_AUDIO_CLOCK_SEL1		(1 << 15)


/*

    There are 8 control bits in the touch screen controller (AD7873)

    S A2 A1 A0 MODE SER/DFR# PD1 PD0

    S 		Start bit, always one.
    A2 - A0	Channel select bits
    MODE	0 => 12 bit resolution, 1 => 8 bit
    SER/DFR#	Single ender/Differential Reference Select bit
    PD1, PD0	Power management bits (usually 10)


From Table 1.

	A2-A0

  	0 Temp0 (SER must be 1)
	1 X+ (is this a typo? - is this X- really?)
	2 VBAT,
	3 read X+ (Z1),
	4 read Y- (Z2), 5 => read Y+,

*/

#define AD7873_START		0x80		/* all commands need this to be set */
#define AD7873_ADDR_BITS	4		/* ie shift by this */
#define AD7873_8BITMODE		0x08		/* 0 => 12 bit convertions */
#define AD7873_SER_DFR		0x04
#define AD7873_PD1		0x02
#define AD7873_PD0		0x01

#define AD7873_TEMP0		AD7873_SER_DFR
#define AD7873_X		(1 << AD7873_ADDR_BITS)
#define AD7873_VBAT		((2 << AD7873_ADDR_BITS) | AD7873_SER_DFR)
#define AD7873_X_Z1		(3 << AD7873_ADDR_BITS)
#define AD7873_Y_Z2		(4 << AD7873_ADDR_BITS)
#define AD7873_Y		(5 << AD7873_ADDR_BITS)
#define AD7873_AUX		((6 << AD7873_ADDR_BITS) | AD7873_SER_DFR)
#define AD7873_TEMP1		((7 << AD7873_ADDR_BITS) | AD7873_SER_DFR)

#ifndef __ASSEMBLY__

extern int storkSetLatchA(int bits);
extern int storkClearLatchA(int bits);

extern int storkSetLatchB(int bits);
extern int storkClearLatchB(int bits);

extern int storkSetLCDCPLD(int which, int bits);
extern int storkClearLCDCPLD(int which, int bits);

extern void storkSetGPIO(int bits);
extern void storkClearGPIO(int bits);

extern int storkGetGPIO(void);

extern void storkClockShortToDtoA(int word);
extern int storkClockByteToTS(int byte);


/* this will return the current state of the hardware ANDED with the given bits
   so NE => at least one bit was set, but maybe not all of them! */

extern int storkTestGPIO(int bits);


#endif

#endif

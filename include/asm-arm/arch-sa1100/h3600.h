/*
*
* Definitions for H3600 Handheld Computer
*
* Copyright 2000 Compaq Computer Corporation.
*
* Use consistent with the GNU GPL is permitted,
* provided that this copyright notice is
* preserved in its entirety in all copies and derived works.
*
* COMPAQ COMPUTER CORPORATION MAKES NO WARRANTIES, EXPRESSED OR IMPLIED,
* AS TO THE USEFULNESS OR CORRECTNESS OF THIS CODE OR ITS
* FITNESS FOR ANY PARTICULAR PURPOSE.
*
* Author: Jamey Hicks.
*
*/

#ifndef _INCLUDE_H3600_H_
#define _INCLUDE_H3600_H_

#define GPIO_H3600_NPOWER_BUTTON	GPIO_GPIO (0)
#define GPIO_H3600_ACTION_BUTTON	GPIO_GPIO (18)

#define GPIO_H3600_PCMCIA_CD0		GPIO_GPIO (17)
#define GPIO_H3600_PCMCIA_CD1		GPIO_GPIO (10)
#define GPIO_H3600_PCMCIA_IRQ0		GPIO_GPIO (21)
#define GPIO_H3600_PCMCIA_IRQ1		GPIO_GPIO (11)

/* audio sample rate clock generator */
#define GPIO_H3600_CLK_SET0		GPIO_GPIO (12)
#define GPIO_H3600_CLK_SET1		GPIO_GPIO (13)

/* UDA1341 L3 Interface */
#define GPIO_H3600_L3_DATA		GPIO_GPIO (14)
#define GPIO_H3600_L3_CLOCK		GPIO_GPIO (16)
#define GPIO_H3600_L3_MODE		GPIO_GPIO (15)

#define GPIO_H3600_OPT_LOCK		GPIO_GPIO (22)
#define GPIO_H3600_OPT_IRQ		GPIO_GPIO (24)
#define GPIO_H3600_OPT_DET		GPIO_GPIO (27)

#define GPIO_H3600_COM_DCD		GPIO_GPIO (23)
#define GPIO_H3600_COM_CTS		GPIO_GPIO (25)
#define GPIO_H3600_COM_RTS		GPIO_GPIO (26)

#define IRQ_GPIO_H3600_NPOWER_BUTTON    IRQ_GPIO0
#define IRQ_GPIO_H3600_ACTION_BUTTON    IRQ_GPIO18
#define IRQ_GPIO_H3600_PCMCIA_CD0	IRQ_GPIO17
#define IRQ_GPIO_H3600_PCMCIA_CD1	IRQ_GPIO10
#define IRQ_GPIO_H3600_PCMCIA_IRQ0	IRQ_GPIO21
#define IRQ_GPIO_H3600_PCMCIA_IRQ1	IRQ_GPIO11
#define IRQ_GPIO_H3600_OPT_IRQ		IRQ_GPIO24
#define IRQ_GPIO_H3600_OPT_DET		IRQ_GPIO27
#define IRQ_GPIO_H3600_COM_DCD          IRQ_GPIO23
#define IRQ_GPIO_H3600_COM_CTS          IRQ_GPIO25

#define EGPIO_H3600_VPP_ON		(1 << 0)
#define EGPIO_H3600_CARD_RESET		(1 << 1)  /* reset the attached pcmcia/compactflash card.  active high. */
#define EGPIO_H3600_OPT_RESET		(1 << 2)  /* reset the attached option pack.  active high. */
#define EGPIO_H3600_CODEC_NRESET	(1 << 3)  /* reset the onboard UDA1341.  active low. */
#define EGPIO_H3600_OPT_NVRAM_ON	(1 << 4)  /* apply power to optionpack nvram, active high. */
#define EGPIO_H3600_OPT_ON		(1 << 5)  /* full power to option pack.  active high. */
#define EGPIO_H3600_LCD_ON       	(1 << 6)  /* enable 3.3V to LCD.  active high. */
#define EGPIO_H3600_RS232_ON		(1 << 7)  /* UART3 transceiver force on.  Active high. */
#define EGPIO_H3600_LCD_PCI		(1 << 8)  /* LCD control IC enable.  active high. */
#define EGPIO_H3600_IR_ON		(1 << 9)  /* apply power to IR module.  active high. */
#define EGPIO_H3600_AUD_AMP_ON		(1 << 10) /* apply power to audio power amp.  active high. */
#define EGPIO_H3600_AUD_PWR_ON		(1 << 11) /* apply poewr to reset of audio circuit.  active high. */
#define EGPIO_H3600_QMUTE		(1 << 12) /* mute control for onboard UDA1341.  active high. */
#define EGPIO_H3600_IR_FSEL		(1 << 13) /* IR speed select: 1->fast, 0->slow */
#define EGPIO_H3600_LCD_5V_ON		(1 << 14) /* enable 5V to LCD. active high. */
#define EGPIO_H3600_LVDD_ON		(1 << 15) /* enable 9V and -6.5V to LCD. */

#ifndef __ASSEMBLY__
#define H3600_EGPIO	(*(volatile int *)0xf0000000)
extern void clr_h3600_egpio(unsigned long x);
extern void set_h3600_egpio(unsigned long x);
#endif

#endif

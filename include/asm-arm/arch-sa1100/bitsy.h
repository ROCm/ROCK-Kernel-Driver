#ifndef _INCLUDE_BITSY_H_
#define _INCLUDE_BITSY_H_

#define GPIO_BITSY_PCMCIA_CD0  GPIO_GPIO (17)
#define GPIO_BITSY_PCMCIA_CD1  GPIO_GPIO (10)
#define GPIO_BITSY_PCMCIA_IRQ0 GPIO_GPIO (21)
#define GPIO_BITSY_PCMCIA_IRQ1 GPIO_GPIO (11)

/* audio sample rate clock generator */
#define GPIO_BITSY_CLK_SET0    GPIO_GPIO (12)
#define GPIO_BITSY_CLK_SET1    GPIO_GPIO (13)
/* UDA1341 L3 Interface */
#define GPIO_BITSY_L3_DATA     GPIO_GPIO (14)
#define GPIO_BITSY_L3_CLOCK    GPIO_GPIO (16)
#define GPIO_BITSY_L3_MODE     GPIO_GPIO (15)


#define IRQ_GPIO_BITSY_PCMCIA_CD0	IRQ_GPIO17
#define IRQ_GPIO_BITSY_PCMCIA_CD1	IRQ_GPIO10
#define IRQ_GPIO_BITSY_PCMCIA_IRQ0	IRQ_GPIO21
#define IRQ_GPIO_BITSY_PCMCIA_IRQ1	IRQ_GPIO22

#define EGPIO_BITSY_VPP_ON      (1 << 0)
#define EGPIO_BITSY_CARD_RESET  (1 << 1)    /* reset the attached pcmcia/compactflash card.  active high. */
#define EGPIO_BITSY_OPT_RESET   (1 << 2)    /* reset the attached option pack.  active high. */
#define EGPIO_BITSY_CODEC_NRESET (1 << 3)   /* reset the onboard UDA1341.  active low. */
#define EGPIO_BITSY_OPT_NVRAM_ON (1 << 4)   /* apply power to optionpack nvram, active high. */
#define EGPIO_BITSY_OPT_ON       (1 << 5)   /* full power to option pack.  active high. */
#define EGPIO_BITSY_LCD_ON       (1 << 6)   /* enable 3.3V to LCD.  active high. */
#define EGPIO_BITSY_RS232_ON     (1 << 7)   /* UART3 transceiver force on.  Active high. */
#define EGPIO_BITSY_LCD_PCI      (1 << 8)   /* LCD control IC enable.  active high. */
#define EGPIO_BITSY_IR_ON        (1 << 9)   /* apply power to IR module.  active high. */
#define EGPIO_BITSY_AUD_AMP_ON   (1 << 10)  /* apply power to audio power amp.  active high. */
#define EGPIO_BITSY_AUD_PWR_ON   (1 << 11)  /* apply poewr to reset of audio circuit.  active high. */
#define EGPIO_BITSY_QMUTE        (1 << 12)  /* mute control for onboard UDA1341.  active high. */
#define EGPIO_BITSY_IR_FSEL      (1 << 13)  /* IR speed select: 1->fast, 0->slow */
#define EGPIO_BITSY_LCD_5V_ON    (1 << 14)  /* enable 5V to LCD. active high. */
#define EGPIO_BITSY_LVDD_ON      (1 << 15)  /* enable 9V and -6.5V to LCD. */

#ifndef __ASSEMBLY__
#define BITSY_EGPIO  (*(volatile int *)0xf0000000)
extern void clr_bitsy_egpio(unsigned long x);
extern void set_bitsy_egpio(unsigned long x);
#endif

#endif

/*
 * linux/include/asm-arm/arch-sa1100/assabet.h
 *
 * Created 2000/06/05 by Nicolas Pitre <nico@cam.org>
 *
 * This file contains the hardware specific definitions for Assabet
 *
 * 2000/05/23 John Dorsey <john+@cs.cmu.edu>
 *      Definitions for Neponset added.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#error "include <asm/hardware.h> instead"
#endif


/* System Configuration Register flags */

#define SCR_SDRAM_LOW	(1<<2)	/* SDRAM size (low bit) */
#define SCR_SDRAM_HIGH	(1<<3)	/* SDRAM size (high bit) */
#define SCR_FLASH_LOW	(1<<4)	/* Flash size (low bit) */
#define SCR_FLASH_HIGH	(1<<5)	/* Flash size (high bit) */
#define SCR_GFX		(1<<8)	/* Graphics Accelerator (0 = present) */
#define SCR_SA1111	(1<<9)	/* Neponset (0 = present) */

#define SCR_INIT	-1


/* Board Control Register */

#define BCR_BASE  0xf1000000
#define BCR (*(volatile unsigned int *)(BCR_BASE))

#define BCR_DB1110	(0x00A07410)
#define BCR_DB1111	(0x00A07462)

#define BCR_CF_PWR	(1<<0)	/* Compact Flash Power (1 = 3.3v, 0 = off) */
#define BCR_CF_RST	(1<<1)	/* Compact Flash Reset (1 = power up reset) */
#define BCR_GFX_RST	(1<<1)	/* Graphics Accelerator Reset (0 = hold reset) */
#define BCR_CODEC_RST	(1<<2)	/* 0 = Holds UCB1300, ADI7171, and UDA1341 in reset */
#define BCR_IRDA_FSEL	(1<<3)	/* IRDA Frequency select (0 = SIR, 1 = MIR/ FIR) */
#define BCR_IRDA_MD0	(1<<4)	/* Range/Power select */
#define BCR_IRDA_MD1	(1<<5)	/* Range/Power select */
#define BCR_STEREO_LB	(1<<6)	/* Stereo Loopback */
#define BCR_CF_BUS_OFF	(1<<7)	/* Compact Flash bus (0 = on, 1 = off (float)) */
#define BCR_AUDIO_ON	(1<<8)	/* Audio power on */
#define BCR_LIGHT_ON	(1<<9)	/* Backlight */
#define BCR_LCD_12RGB	(1<<10)	/* 0 = 16RGB, 1 = 12RGB */
#define BCR_LCD_ON	(1<<11)	/* LCD power on */
#define BCR_RS232EN	(1<<12)	/* RS232 transceiver enable */
#define BCR_LED_RED	(1<<13)	/* D9 (0 = on, 1 = off) */
#define BCR_LED_GREEN	(1<<14)	/* D8 (0 = on, 1 = off) */
#define BCR_VIB_ON	(1<<15)	/* Vibration motor (quiet alert) */
#define BCR_COM_DTR	(1<<16)	/* COMport Data Terminal Ready */
#define BCR_COM_RTS	(1<<17)	/* COMport Request To Send */
#define BCR_RAD_WU	(1<<18)	/* Radio wake up interrupt */
#define BCR_SMB_EN	(1<<19)	/* System management bus enable */
#define BCR_TV_IR_DEC	(1<<20)	/* TV IR Decode Enable */
#define BCR_QMUTE	(1<<21)	/* Quick Mute */
#define BCR_RAD_ON	(1<<22)	/* Radio Power On */
#define BCR_SPK_OFF	(1<<23)	/* 1 = Speaker amplifier power off */

#ifndef __ASSEMBLY__
extern unsigned long SCR_value;
extern unsigned long BCR_value;
#define BCR_set( x )	BCR = (BCR_value |= (x))
#define BCR_clear( x )	BCR = (BCR_value &= ~(x))
#endif


/* GPIOs for which the generic definition doesn't say much */
#define GPIO_RADIO_IRQ		GPIO_GPIO (14)	/* Radio interrupt request  */
#define GPIO_L3_I2C_SDA		GPIO_GPIO (15)	/* L3 and SMB control ports */
#define GPIO_PS_MODE_SYNC	GPIO_GPIO (16)	/* Power supply mode/sync   */
#define GPIO_L3_MODE		GPIO_GPIO (17)	/* L3 mode signal with LED  */
#define GPIO_L3_I2C_SCL		GPIO_GPIO (18)	/* L3 and I2C control ports */
#define GPIO_STEREO_64FS_CLK	GPIO_GPIO (19)	/* SSP UDA1341 clock input  */
#define GPIO_CF_IRQ		GPIO_GPIO (21)	/* CF IRQ   */
#define GPIO_MBGNT		GPIO_GPIO (21)	/* 1111 MBGNT */
#define GPIO_CF_CD		GPIO_GPIO (22)	/* CF CD */
#define GPIO_MBREQ		GPIO_GPIO (22)	/* 1111 MBREQ */
#define GPIO_UCB1300_IRQ	GPIO_GPIO (23)	/* UCB GPIO and touchscreen */
#define GPIO_CF_BVD2		GPIO_GPIO (24)	/* CF BVD */
#define GPIO_GFX_IRQ		GPIO_GPIO (24)	/* Graphics IRQ */
#define GPIO_CF_BVD1		GPIO_GPIO (25)	/* CF BVD */
#define GPIO_NEP_IRQ		GPIO_GPIO (25)	/* Neponset IRQ */
#define GPIO_BATT_LOW		GPIO_GPIO (26)	/* Low battery */
#define GPIO_RCLK		GPIO_GPIO (26)	/* CCLK/2  */

#define IRQ_GPIO_CF_IRQ		IRQ_GPIO21
#define IRQ_GPIO_CF_CD		IRQ_GPIO22
#define IRQ_GPIO_UCB1300_IRQ	IRQ_GPIO23
#define IRQ_GPIO_CF_BVD2	IRQ_GPIO24
#define IRQ_GPIO_CF_BVD1	IRQ_GPIO25
#define IRQ_GPIO_NEP_IRQ	IRQ_GPIO25


/*
 * Neponset definitions: 
 */

#define SA1111_BASE             (0x40000000)

#define NEPONSET_ETHERNET_IRQ	MISC_IRQ0
#define NEPONSET_USAR_IRQ	MISC_IRQ1

#define NEPONSET_CPLD_BASE      (0x10000000)
#define Nep_p2v( x )            ((x) - NEPONSET_CPLD_BASE + 0xf0000000)
#define Nep_v2p( x )            ((x) - 0xf0000000 + NEPONSET_CPLD_BASE)

#define _IRR                    0x10000024      /* Interrupt Reason Register */
#define _AUD_CTL                0x100000c0      /* Audio controls (RW)       */
#define _MDM_CTL_0              0x100000b0      /* Modem control 0 (RW)      */
#define _MDM_CTL_1              0x100000b4      /* Modem control 1 (RW)      */
#define _NCR_0	                0x100000a0      /* Control Register (RW)     */
#define _KP_X_OUT               0x10000090      /* Keypad row write (RW)     */
#define _KP_Y_IN                0x10000080      /* Keypad column read (RO)   */
#define _SWPK                   0x10000020      /* Switch pack (RO)          */
#define _WHOAMI                 0x10000000      /* System ID Register (RO)   */

#define _LEDS                   0x10000010      /* LEDs [31:0] (WO)          */

#ifndef __ASSEMBLY__

#define IRR                     (*((volatile u_char *) Nep_p2v(_IRR)))
#define AUD_CTL                 (*((volatile u_char *) Nep_p2v(_AUD_CTL)))
#define MDM_CTL_0               (*((volatile u_char *) Nep_p2v(_MDM_CTL_0)))
#define MDM_CTL_1               (*((volatile u_char *) Nep_p2v(_MDM_CTL_1)))
#define NCR_0			(*((volatile u_char *) Nep_p2v(_NCR_0)))
#define KP_X_OUT                (*((volatile u_char *) Nep_p2v(_KP_X_OUT)))
#define KP_Y_IN                 (*((volatile u_char *) Nep_p2v(_KP_Y_IN)))
#define SWPK                    (*((volatile u_char *) Nep_p2v(_SWPK)))
#define WHOAMI                  (*((volatile u_char *) Nep_p2v(_WHOAMI)))

#define LEDS                    (*((volatile Word   *) Nep_p2v(_LEDS)))

#endif

#define IRR_ETHERNET		(1<<0)
#define IRR_USAR		(1<<1)
#define IRR_SA1111		(1<<2)

#define NCR_GP01_OFF		(1<<0)
#define NCR_TP_PWR_EN		(1<<1)
#define NCR_MS_PWR_EN		(1<<2)
#define NCR_ENET_OSC_EN		(1<<3)
#define NCR_SPI_KB_WK_UP	(1<<4)
#define NCR_A0VPP		(1<<5)
#define NCR_A1VPP		(1<<6)

#ifndef __ASSEMBLY__
#define machine_has_neponset()  ((SCR_value & SCR_SA1111) == 0)
#endif


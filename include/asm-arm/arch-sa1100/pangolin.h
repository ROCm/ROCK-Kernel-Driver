/*
 * linux/include/asm-arm/arch-sa1100/pangolin.h
 *
 * Created 2000/08/25 by Murphy Chen <murphy@mail.dialogue.com.tw>
 *
 * This file contains the hardware specific definitions for Pangolin
 *
 */

#ifndef __ASM_ARCH_HARDWARE_H
#error "include <asm/hardware.h> instead"
#endif


/* GPIOs for which the generic definition doesn't say much */
#define GPIO_CF_BUS_ON		GPIO_GPIO (3)
#define GPIO_CF_RESET		GPIO_GPIO (2)
#define GPIO_CF_CD		GPIO_GPIO (22)
#define GPIO_CF_IRQ		GPIO_GPIO (21)

#define IRQ_GPIO_CF_IRQ		IRQ_GPIO21
#define IRQ_GPIO_CF_CD		IRQ_GPIO22

/*
 * include/asm-ppc/serial.h
 */

#ifdef __KERNEL__
#ifndef __ASM_SERIAL_H__
#define __ASM_SERIAL_H__

#include <linux/config.h>

#if defined(CONFIG_EV64260)
#include <platforms/ev64260.h>
#elif defined(CONFIG_GEMINI)
#include <platforms/gemini_serial.h>
#elif defined(CONFIG_POWERPMC250)
#include <platforms/powerpmc250_serial.h>
#elif defined(CONFIG_LOPEC)
#include <platforms/lopec_serial.h>
#elif defined(CONFIG_MCPN765)
#include <platforms/mcpn765_serial.h>
#elif defined(CONFIG_MVME5100)
#include <platforms/mvme5100_serial.h>
#elif defined(CONFIG_PRPMC750)
#include <platforms/prpmc750_serial.h>
#elif defined(CONFIG_PRPMC800)
#include <platforms/prpmc800_serial.h>
#elif defined(CONFIG_SANDPOINT)
#include <platforms/sandpoint.h>
#elif defined(CONFIG_SPRUCE)
#include <platforms/spruce_serial.h>
#elif defined(CONFIG_4xx)
#include <asm/ibm4xx.h>
#else

/*
 * XXX Assume for now it has PC-style ISA serial ports.
 * This is true for PReP and CHRP at least.
 */
#include <asm/pc_serial.h>

#if defined(CONFIG_MAC_SERIAL)
#define SERIAL_DEV_OFFSET	((_machine == _MACH_prep || _machine == _MACH_chrp) ? 0 : 2)
#endif

#endif /* !CONFIG_GEMINI and others */
#endif /* __ASM_SERIAL_H__ */
#endif /* __KERNEL__ */

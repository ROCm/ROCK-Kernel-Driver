/*
 * include/asm-arm/arch-iop3xx/serial.h
 */
#include <linux/config.h>

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD ( 1843200 / 16 )

/* Standard COM flags */
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

#ifdef CONFIG_ARCH_IQ80310

#define IRQ_UART1	IRQ_IQ80310_UART1
#define IRQ_UART2	IRQ_IQ80310_UART2

#define STD_SERIAL_PORT_DEFNS			\
       /* UART CLK      PORT        IRQ        FLAGS        */			\
	{ 0, BASE_BAUD, IQ80310_UART2, IRQ_UART2, STD_COM_FLAGS },  /* ttyS0 */	\
	{ 0, BASE_BAUD, IQ80310_UART1, IRQ_UART1, STD_COM_FLAGS }  /* ttyS1 */

#endif // CONFIG_ARCH_IQ80310

#ifdef CONFIG_ARCH_IQ80321

#define IRQ_UART1	IRQ_IQ80321_UART

#define STD_SERIAL_PORT_DEFNS			\
       /* UART CLK      PORT        IRQ        FLAGS        */			\
	{ 0, BASE_BAUD, 0xfe800000, IRQ_UART1, STD_COM_FLAGS },  /* ttyS0 */
#endif // CONFIG_ARCH_IQ80321


#define EXTRA_SERIAL_PORT_DEFNS


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

#ifdef CONFIG_ARCH_IQ80321

#define IRQ_UART1	IRQ_IQ80321_UART

#define RS_TABLE_SIZE 1

#define STD_SERIAL_PORT_DEFNS			\
       /* UART CLK      PORT        IRQ        FLAGS        */			\
	{ 0, BASE_BAUD, 0xfe800000, IRQ_UART1, STD_COM_FLAGS },  /* ttyS0 */
#endif // CONFIG_ARCH_IQ80321

#ifdef CONFIG_ARCH_IQ31244

#define IRQ_UART1	IRQ_IQ31244_UART

#define RS_TABLE_SIZE 1

#define STD_SERIAL_PORT_DEFNS			\
       /* UART CLK      PORT        IRQ        FLAGS        */			\
	{ 0, BASE_BAUD, 0xfe800000, IRQ_UART1, STD_COM_FLAGS },  /* ttyS0 */
#endif // CONFIG_ARCH_IQ31244

#ifdef CONFIG_ARCH_IQ80331

#undef BASE_BAUD

#define BASE_BAUD ( 33334000 / 16 )

#define IRQ_UART0	IRQ_IQ80331_UART0
#define IRQ_UART1	IRQ_IQ80331_UART1

#define RS_TABLE_SIZE 2

#define STD_SERIAL_PORT_DEFNS				\
	{						\
	  /*type: PORT_XSCALE,*/			\
	  /*xmit_fifo_size: 32,*/			\
	  baud_base: BASE_BAUD,				\
	  irq: IRQ_UART0,			  	\
	  flags: STD_COM_FLAGS,				\
	  iomem_base: IQ80331_UART0_VIRT,		\
	  io_type: SERIAL_IO_MEM,			\
	  iomem_reg_shift: 2				\
	}, /* ttyS0 */					\
	{						\
	  /*type: PORT_XSCALE,*/			\
	  /*xmit_fifo_size: 32,*/			\
	  baud_base: BASE_BAUD,				\
	  irq: IRQ_UART1,			  	\
	  flags: STD_COM_FLAGS,				\
	  iomem_base: IQ80331_UART1_VIRT,		\
	  io_type: SERIAL_IO_MEM,			\
	  iomem_reg_shift: 2				\
	} /* ttyS1 */
#endif // CONFIG_ARCH_IQ80331


#define EXTRA_SERIAL_PORT_DEFNS


/*
 * COM1 NS16550 support
 */

#include "ns16550.h"
typedef struct NS16550 *NS16550_t;

const NS16550_t COM_PORTS[] = { (NS16550_t) COM1,
    (NS16550_t) COM2,
    (NS16550_t) COM3,
    (NS16550_t) COM4 };

volatile struct NS16550 *
NS16550_init(int chan)
{
 volatile struct NS16550 *com_port;
 volatile unsigned char xx;
 com_port = (struct NS16550 *) COM_PORTS[chan];
 /* See if port is present */
 com_port->lcr = 0x00;
 com_port->ier = 0xFF;
#if 0
 if (com_port->ier != 0x0F) return ((struct NS16550 *)0);
#endif
 com_port->ier = 0x00;
 com_port->lcr = 0x80;  /* Access baud rate */
 com_port->dll = 0xc;  /* 9600 baud */
 com_port->dlm = 0xc >> 8;
 com_port->lcr = 0x03;  /* 8 data, 1 stop, no parity */
 com_port->mcr = 0x03;  /* RTS/DTR */
 com_port->fcr = 0x07;  /* Clear & enable FIFOs */
 return (com_port);
}


NS16550_putc(volatile struct NS16550 *com_port, unsigned char c)
{
 volatile int i;
 while ((com_port->lsr & LSR_THRE) == 0) ;
 com_port->thr = c;
}

unsigned char
NS16550_getc(volatile struct NS16550 *com_port)
{
 while ((com_port->lsr & LSR_DR) == 0) ;
 return (com_port->rbr);
}

NS16550_tstc(volatile struct NS16550 *com_port)
{
 return ((com_port->lsr & LSR_DR) != 0);
}




/*
 *  arch/i386/mach-pc9800/io_ports.h
 *
 *  Machine specific IO port address definition for PC-9800.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef _MACH_IO_PORTS_H
#define _MACH_IO_PORTS_H

/* i8253A PIT registers */
#define PIT_MODE		0x77
#define PIT_CH0			0x71
#define PIT_CH2			0x75

/* i8259A PIC registers */
#define PIC_MASTER_CMD		0x00
#define PIC_MASTER_IMR		0x02
#define PIC_MASTER_ISR		PIC_MASTER_CMD
#define PIC_MASTER_POLL		PIC_MASTER_ISR
#define PIC_MASTER_OCW3		PIC_MASTER_ISR
#define PIC_SLAVE_CMD		0x08
#define PIC_SLAVE_IMR		0x0a

/* i8259A PIC related values */
#define PIC_CASCADE_IR		7
#define MASTER_ICW4_DEFAULT	0x1d
#define SLAVE_ICW4_DEFAULT	0x09
#define PIC_ICW4_AEOI		0x02

#endif /* !_MACH_IO_PORTS_H */

/*
 *  Constant and architecture independent procedures
 *  for NEC uPD4990A serial I/O real-time clock.
 *
 *  Copyright 2001  TAKAI Kousuke <tak@kmc.kyoto-u.ac.jp>
 *		    Kyoto University Microcomputer Club (KMC).
 *
 *  References:
 *	uPD4990A serial I/O real-time clock users' manual (Japanese)
 *	No. S12828JJ4V0UM00 (4th revision), NEC Corporation, 1999.
 */

#ifndef _LINUX_uPD4990A_H
#define _LINUX_uPD4990A_H

#include <asm/byteorder.h>

#include <asm/upd4990a.h>

/* Serial commands (4 bits) */
#define UPD4990A_REGISTER_HOLD			(0x0)
#define UPD4990A_REGISTER_SHIFT			(0x1)
#define UPD4990A_TIME_SET_AND_COUNTER_HOLD	(0x2)
#define UPD4990A_TIME_READ			(0x3)
#define UPD4990A_TP_64HZ			(0x4)
#define UPD4990A_TP_256HZ			(0x5)
#define UPD4990A_TP_2048HZ			(0x6)
#define UPD4990A_TP_4096HZ			(0x7)
#define UPD4990A_TP_1S				(0x8)
#define UPD4990A_TP_10S				(0x9)
#define UPD4990A_TP_30S				(0xA)
#define UPD4990A_TP_60S				(0xB)
#define UPD4990A_INTERRUPT_RESET		(0xC)
#define UPD4990A_INTERRUPT_TIMER_START		(0xD)
#define UPD4990A_INTERRUPT_TIMER_STOP		(0xE)
#define UPD4990A_TEST_MODE_SET			(0xF)

/* Parallel commands (3 bits)
   0-6 are same with serial commands.  */
#define UPD4990A_PAR_SERIAL_MODE		7

#ifndef UPD4990A_DELAY
# include <linux/delay.h>
# define UPD4990A_DELAY(usec)	udelay((usec))
#endif
#ifndef UPD4990A_OUTPUT_DATA
# define UPD4990A_OUTPUT_DATA(bit)			\
	do {						\
		UPD4990A_OUTPUT_DATA_CLK((bit), 0);	\
		UPD4990A_DELAY(1); /* t-DSU */		\
		UPD4990A_OUTPUT_DATA_CLK((bit), 1);	\
		UPD4990A_DELAY(1); /* t-DHLD */	\
	} while (0)
#endif

static __inline__ void upd4990a_serial_command(int command)
{
	UPD4990A_OUTPUT_DATA(command >> 0);
	UPD4990A_OUTPUT_DATA(command >> 1);
	UPD4990A_OUTPUT_DATA(command >> 2);
	UPD4990A_OUTPUT_DATA(command >> 3);
	UPD4990A_DELAY(1);	/* t-HLD */
	UPD4990A_OUTPUT_STROBE(1);
	UPD4990A_DELAY(1);	/* t-STB & t-d1 */
	UPD4990A_OUTPUT_STROBE(0);
	/* 19 microseconds extra delay is needed
	   iff previous mode is TIME READ command  */
}

struct upd4990a_raw_data {
	u8	sec;		/* BCD */
	u8	min;		/* BCD */
	u8	hour;		/* BCD */
	u8	mday;		/* BCD */
#if   defined __LITTLE_ENDIAN_BITFIELD
	unsigned wday :4;	/* 0-6 */
	unsigned mon :4;	/* 1-based */
#elif defined __BIG_ENDIAN_BITFIELD
	unsigned mon :4;	/* 1-based */
	unsigned wday :4;	/* 0-6 */
#else
# error Unknown bitfield endian!
#endif
	u8	year;		/* BCD */
};

static __inline__ void upd4990a_get_time(struct upd4990a_raw_data *buf,
					  int leave_register_hold)
{
	int byte;

	upd4990a_serial_command(UPD4990A_TIME_READ);
	upd4990a_serial_command(UPD4990A_REGISTER_SHIFT);
	UPD4990A_DELAY(19);	/* t-d2 - t-d1 */

	for (byte = 0; byte < 6; byte++) {
		u8 tmp;
		int bit;

		for (tmp = 0, bit = 0; bit < 8; bit++) {
			tmp = (tmp | (UPD4990A_READ_DATA() << 8)) >> 1;
			UPD4990A_OUTPUT_CLK(1);
			UPD4990A_DELAY(1);
			UPD4990A_OUTPUT_CLK(0);
			UPD4990A_DELAY(1);
		}
		((u8 *) buf)[byte] = tmp;
	}

	/* The uPD4990A users' manual says that we should issue `Register
	   Hold' command after each data retrieval, or next `Time Read'
	   command may not work correctly.  */
	if (!leave_register_hold)
		upd4990a_serial_command(UPD4990A_REGISTER_HOLD);
}

static __inline__ void upd4990a_set_time(const struct upd4990a_raw_data *data,
					  int time_set_only)
{
	int byte;

	if (!time_set_only)
		upd4990a_serial_command(UPD4990A_REGISTER_SHIFT);

	for (byte = 0; byte < 6; byte++) {
		int bit;
		u8 tmp = ((const u8 *) data)[byte];

		for (bit = 0; bit < 8; bit++, tmp >>= 1)
			UPD4990A_OUTPUT_DATA(tmp);
	}

	upd4990a_serial_command(UPD4990A_TIME_SET_AND_COUNTER_HOLD);

	/* Release counter hold and start the clock.  */
	if (!time_set_only)
		upd4990a_serial_command(UPD4990A_REGISTER_HOLD);
}

#endif /* _LINUX_uPD4990A_H */

/*
 *  linux/arch/mips/philips/nino/kgdb.c
 *
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Kernel debugging on the Philips Nino.
 */
#include <asm/system.h>
#include <asm/tx3912.h>

static int remoteDebugInitialized = 0;

void debugInit(void)
{
/*
 * If low-level debugging (before GDB or console operational) is
 * configured, then we do not need to re-initialize the UART.
 */
#ifndef CONFIG_DEBUG_LL
	earlyInitUartPR31700();
#endif
}

char getDebugChar(void)
{
	char buf;
	unsigned long int2, flags;

	if (!remoteDebugInitialized) {
		debugInit();
		remoteDebugInitialized = 1;
	}

	save_and_cli(flags);

	int2 = IntEnable2;

	IntEnable2 = 0;

	while(!(UartA_Ctrl1 & UART_RX_HOLD_FULL));

	buf = UartA_Data;

	IntEnable2 = int2;

	restore_flags(flags);

	return buf;	
}

int putDebugChar(char c)
{
	int i;
	unsigned long int2;

	if (!remoteDebugInitialized) {
		debugInit();
		remoteDebugInitialized = 1;
	}

	int2 = IntEnable2;

	IntEnable2 &=
		~(INT2_UARTATXINT | INT2_UARTATXOVERRUN | INT2_UARTAEMPTY);

	for (i = 0; !(IntStatus2 & INT2_UARTATXINT) && (i < 10000); i++);

	IntClear2 = INT2_UARTATXINT | INT2_UARTATXOVERRUN | INT2_UARTAEMPTY;

	UartA_Data = c;

	for (i = 0; !(IntStatus2 & INT2_UARTATXINT) && (i < 10000); i++);

	IntClear2 = INT2_UARTATXINT | INT2_UARTATXOVERRUN | INT2_UARTAEMPTY;

	IntEnable2 = int2;

	return 1;
}

/*
 * Filename: cobaltscc.c
 * 
 * Description: Functions for supporting and testing serial I/O
 * 
 * Author(s): Timothy Stonis
 * 
 * Copyright 1997, Cobalt Microserver, Inc.
 */
#include "z8530.h"
#include "diagdefs.h"
#include "serial.h"
#include "asm/io.h"

/*
 * Function prototypes
 */
void InitSerialPort(unsigned char *);
void RegisterDelay(void);
void InitScc(void);

/*
 * Function: RegisterDelay
 *
 * Description: A little delay since the SCC can't handle quick consecutive 
 *              accesses
 * In: none
 * Out: none
 */
void RegisterDelay(void)
{
	register int ctr;
  
	for(ctr=0;ctr<0x40;ctr++);
}

/*
 * Function: SccInit
 *
 * Description: Initialize all the SCC registers for 19200 baud, asynchronous,
 *		8 bit, 1 stop bit, no parity communication (Channel A)
 *
 * In: none
 *
 * Out: none
 */
void InitScc(void)
{
	/* Force hardware reset */
	Write8530(kSCC_ChanA | kSCC_Command, R9 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, FHWRES);
	RegisterDelay();
  
	/* x32 clock, 1 stop bit, no parity */
	Write8530(kSCC_ChanA | kSCC_Command, R4 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, X16CLK | SB1);
	RegisterDelay();
  
	/* Rx 8 bits, Rx disabled */
	Write8530(kSCC_ChanA | kSCC_Command, R3 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, Rx8);
	RegisterDelay();
   
	/* Tx 8 bits, DTR, RTS, Tx off */
	Write8530(kSCC_ChanA | kSCC_Command, R5 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, Tx8 | DTR | RTS);
	RegisterDelay();

	/* Int. Disabled */
	Write8530(kSCC_ChanA | kSCC_Command, R9 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, 0x0);
	RegisterDelay();

	/* NRZ */
	Write8530(kSCC_ChanA | kSCC_Command, R10 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, NRZ);
	RegisterDelay();

	/* Tx & Rx = BRG out, TRxC = BRG out */
	Write8530(kSCC_ChanA | kSCC_Command, R11 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, TCBR | RCBR | TRxCBR | TRxCOI); 
	RegisterDelay();

	/* Time constant = 0x01 */
	Write8530(kSCC_ChanA | kSCC_Command, R12 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, kSCC_115200 ); 
	RegisterDelay();

	/* Time constant high = 0x00 */
	Write8530(kSCC_ChanA | kSCC_Command, R13 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, 0x00); 
	RegisterDelay();

	/* BRG in = ~RTxC, BRG off, loopback */
	Write8530(kSCC_ChanA | kSCC_Command, R14 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, LOOPBAK | BRSRC); 
	RegisterDelay();
}

/*
 * Function: EnableScc
 *
 * Description: Enable transmit and receive on SCC Channel A
 * In: none
 * Out: none
 */
void EnableScc(void)
{
	/* Enable BRG */
	Write8530(kSCC_ChanA | kSCC_Command, R14 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, BRENABL | BRSRC);
	RegisterDelay();
  
	/* Rx enable (Rx 8 bits) */
	Write8530(kSCC_ChanA | kSCC_Command, R3 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, RxENABLE | Rx8);
	RegisterDelay();

	/* Tx enable (Tx8, DTR, RTS) */
	Write8530(kSCC_ChanA | kSCC_Command, R5 | NULLCODE);
	RegisterDelay();
	Write8530(kSCC_ChanA, TxENAB | Tx8 | DTR | RTS); 
	RegisterDelay();
}

/*
 * Function: SccOutb
 *
 * Description: Write a byte to the SCC (Channel A) and blink LED
 * In: Byte to send
 * Out: none
 */
void SccOutb(unsigned char byte)
{
	/* LED on.. */
	Write8530(kSCC_ChanB | kSCC_Command, R5);
	RegisterDelay();
	Write8530(kSCC_ChanB | kSCC_Command, RTS);
	RegisterDelay();
 
	while ((Read8530(kSCC_ChanA) & Tx_BUF_EMP) == 0)
		RegisterDelay();
 
	Write8530(kSCC_ChanA | kSCC_Direct, byte);
	RegisterDelay();
 
	/* LED off.. */
	Write8530(kSCC_ChanB | kSCC_Command, R9);
	RegisterDelay();
	Write8530(kSCC_ChanB | kSCC_Command, CHRB);
	RegisterDelay();
}

/*
 * Function: SccInb
 *
 * Description: Read a byte from the SCC (Channel A)
 * In: Byte to send
 * Out: none
 */
void SccInb(unsigned char *byte)
{
	while ((Read8530(kSCC_ChanA) & Rx_CH_AV) == 0)
		RegisterDelay();
 
	*byte = Read8530(kSCC_ChanA | kSCC_Direct);
	RegisterDelay();
}

/*
 * Function: SccWrite
 *
 * Description: Write a null terminated string to the SCC 
 * In: C string
 * Out: none
 */
void SccWrite(const unsigned char *string)
{
	while((*string) != 0) { 
		if (*string == 10)
			SccOutb((unsigned char) 13);
		SccOutb(*(string++));
	}
}

/*
 * Function: InitSerialPort
 *
 * Description: Initialize the SCC and spit out the header message 
 * In: Header message
 * Out: none
 */
void InitSerialPort(unsigned char *msg)
{
	InitScc();
	EnableScc();
	SccWrite(msg);
}

/*
 * Function: SccInbTimeout
 *
 * Description: Read a byte from the SCC (Channel A) with timeout
 * In: Byte to send
 * Out: Timeout status
 */
unsigned char SccInbTimeout(unsigned char *byte, unsigned long timeout)
{
	unsigned long ctr = 0;

	while ((Read8530(kSCC_ChanA) & Rx_CH_AV) == 0) {
		RegisterDelay();
		if ((ctr++) > timeout)
			return 0xFF;
	}

	*byte = Read8530(kSCC_ChanA | kSCC_Direct);
	RegisterDelay();
 
	return 0;
}

#include <linux/serial_reg.h>

extern int serial_echo_init (int base);
extern int serial_echo_print (const char *s);

/*
 * this defines the address for the port to which printk echoing is done
 *  when CONFIG_SERIAL_ECHO is defined
 */
#define SERIAL_ECHO_PORT       0x1C800000 

static int serial_echo_port = 0;

#define serial_echo_outb(v,a) outb((v),(a)+serial_echo_port)
#define serial_echo_inb(a)    inb((a)+serial_echo_port)

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

/* Wait for transmitter & holding register to empty */
#define WAIT_FOR_XMITR \
 do { \
       lsr = serial_echo_inb(UART_LSR); \
 } while ((lsr & BOTH_EMPTY) != BOTH_EMPTY)

/*
 * These two functions abstract the actual communications with the
 * debug port.  This is so we can change the underlying communications
 * mechanism without modifying the rest of the code.
 */
int
serial_echo_print(const char *s)
{
        int     lsr, ier;
        int     i;

        if (!serial_echo_port)
		return 0;

        /*
         * First save the IER then disable the interrupts
         */
        ier = serial_echo_inb(UART_IER);
        serial_echo_outb(0x00, UART_IER);

        /*
         * Now, do each character
         */
        for (i = 0; *s; i++, s++) {
                WAIT_FOR_XMITR;

                /* Send the character out. */
                serial_echo_outb(*s, UART_TX);

                /* if a LF, also do CR... */
                if (*s == 10) {
                        WAIT_FOR_XMITR;
                        serial_echo_outb(13, UART_TX);
                }
        }

        /*
         * Finally, Wait for transmitter & holding register to empty
         *  and restore the IER
         */
        do {
                lsr = serial_echo_inb(UART_LSR);
        } while ((lsr & BOTH_EMPTY) != BOTH_EMPTY);
        serial_echo_outb(ier, UART_IER);

        return 0;
}


int
serial_echo_init(int base)
{
        int comstat, hi, lo;

        serial_echo_port = base;

        /*
         * read the Divisor Latch
         */
        comstat = serial_echo_inb(UART_LCR);
        serial_echo_outb(comstat | UART_LCR_DLAB, UART_LCR);
        hi = serial_echo_inb(UART_DLM);
        lo = serial_echo_inb(UART_DLL);
        serial_echo_outb(comstat, UART_LCR);

        /*
         * now do hardwired init
         */
        serial_echo_outb(0x03, UART_LCR); /* No parity, 8 data bits, 1 stop */
        serial_echo_outb(0x83, UART_LCR); /* Access divisor latch */

	/* This is computed using:
	 *
	 * const BASE_BAUD = (18432000 / 16);
	 * UART_DLM = (BASE_BAUD / baud_I_want) >> 8;
	 * UART_DLL = (BASE_BAUD / baud_I_want) & 0xff;
	 */
        serial_echo_outb(0x00, UART_DLM); /* 115200 baud */
        serial_echo_outb(0x0A, UART_DLL);

        serial_echo_outb(0x03, UART_LCR); /* Done with divisor */

        /*
	 * Prior to disabling interrupts, read the LSR and RBR
         * registers
         */
        comstat = serial_echo_inb(UART_LSR); /* COM? LSR */
        comstat = serial_echo_inb(UART_RX);     /* COM? RBR */
        serial_echo_outb(0x00, UART_IER); /* Disable all interrupts */

        return 0;
}

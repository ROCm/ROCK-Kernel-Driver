/*
 * 
 * Filename: serial.h
 * 
 * Description: Some general definitions used for serial code 
 * 
 * Author(s): Timothy Stonis
 * 
 * Copyright 1997, Cobalt Microserver, Inc.
 */
 
/*
 * Serial port definitions
 */
#define kSCC_Base	kGal_DevBank1Base
#define kHelloWorldMsg	"Cobalt Networks Diagnostics - 'We serve it, you surf it'\n\r"
#define kSCC_ChanA	0x01
#define kSCC_ChanB	0x00
#define kSCC_Direct	0x02
#define kSCC_Command	0x00

#define kSCC_TestVal	0xA5
#define kSCC_19200	0x07	/* x32 clock mode, 19200 baud 	*/
#define kSCC_115200	0x01	/* x16 clock mode, 115200 baud 	*/

#define Read8530(n)	(*((unsigned char *) (kSCC_Base | (n))))

#define Write8530(x,y)	(*((unsigned char *) (kSCC_Base | (x))) = (y))

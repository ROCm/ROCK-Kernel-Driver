/* $Id: i2c.h,v 1.2 2001/01/18 15:49:30 bjornw Exp $ */

/* High level I2C actions */
int i2c_writereg(unsigned char theSlave, unsigned char theReg, unsigned char theValue);
unsigned char i2c_readreg(unsigned char theSlave, unsigned char theReg);

/* Low level I2C */
static void i2c_start(void);
static void i2c_stop(void);
static void i2c_outbyte(unsigned char x);
static unsigned char i2c_inbyte(void);
static int i2c_getack(void);
static void i2c_sendack(void);




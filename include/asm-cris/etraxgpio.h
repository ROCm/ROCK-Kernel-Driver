#ifndef _ASM_ETRAXGPIO_H
#define _ASM_ETRAXGPIO_H

/* etraxgpio _IOC_TYPE, bits 8 to 15 in ioctl cmd */

#define ETRAXGPIO_IOCTYPE 43

/* supported ioctl _IOC_NR's */

#define IO_READBITS  0x1  /* read and return current port bits */
#define IO_SETBITS   0x2  /* set the bits marked by 1 in the argument */
#define IO_CLRBITS   0x3  /* clear the bits marked by 1 in the argument */

/* the alarm is waited for by select() */

#define IO_HIGHALARM 0x4  /* set alarm on high for bits marked by 1 */
#define IO_LOWALARM  0x5  /* set alarm on low for bits marked by 1 */
#define IO_CLRALARM  0x6  /* clear alarm for bits marked by 1 */

/* LED ioctl */
#define IO_LEDACTIVE_SET 0x7 /* set active led
                              * 0=off, 1=green, 2=red, 3=yellow */

/* GPIO direction ioctl's */
#define IO_READDIR    0x8  /* Read direction 0=input 1=output */
#define IO_SETINPUT   0x9  /* Set direction 0=unchanged 1=input, 
                              returns current dir */
#define IO_SETOUTPUT  0xA  /* Set direction 0=unchanged 1=output,
                              returns current dir */

/* LED ioctl extended */
#define IO_LED_SETBIT 0xB
#define IO_LED_CLRBIT 0xC

/* SHUTDOWN ioctl */
#define IO_SHUTDOWN   0xD
#define IO_GET_PWR_BT 0xE

/* Bit toggling in driver settings */
/* bit set in low byte0 is CLK mask (0x00FF), 
   bit set in byte1 is DATA mask    (0xFF00) 
   msb, data_mask[7:0] , clk_mask[7:0]
 */
#define IO_CFG_WRITE_MODE 0xF 
#define IO_CFG_WRITE_MODE_VALUE(msb, data_mask, clk_mask) \
  ( (((msb)&1) << 16) | (((data_mask) &0xFF) << 8) | ((clk_mask) & 0xFF) )


#endif

/*
 *  Registers for the SGS-Thomson M48T35 Timekeeper RAM chip
 */

#ifndef __PPC_M48T35_H
#define __PPC_M48T35_H

/* RTC offsets */
#define M48T35_RTC_CONTROL  0
#define M48T35_RTC_SECONDS  1
#define M48T35_RTC_MINUTES  2
#define M48T35_RTC_HOURS    3
#define M48T35_RTC_DAY      4
#define M48T35_RTC_DOM      5
#define M48T35_RTC_MONTH    6
#define M48T35_RTC_YEAR     7

#define M48T35_RTC_SET      0x80
#define M48T35_RTC_STOPPED  0x80
#define M48T35_RTC_READ     0x40

#ifndef BCD_TO_BIN
#define BCD_TO_BIN(x)   ((x)=((x)&15) + ((x)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(x)   ((x)=(((x)/10)<<4) + (x)%10)
#endif

#endif

#ifndef __ALPHA_DELAY_H
#define __ALPHA_DELAY_H

extern void __delay(int loops);
extern void __udelay(unsigned long usecs, unsigned long lpj);
extern void udelay(unsigned long usecs);

#endif /* defined(__ALPHA_DELAY_H) */

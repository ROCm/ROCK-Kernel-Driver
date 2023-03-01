#ifndef AMDKCL_DELAY_H
#define AMDKCL_DELAY_H

#ifndef HAVE_FSLEEP
static inline void _kcl_fsleep(unsigned long usecs)
{
       if (usecs <= 10)
               udelay(usecs);
       else if (usecs <= 20000)
               usleep_range(usecs, 2 * usecs);
       else
               msleep(DIV_ROUND_UP(usecs, 1000));
}

#define fsleep _kcl_fsleep

#endif
#endif

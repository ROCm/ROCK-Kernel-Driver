/* machine depend header */

/* TIMER rate define */
#ifdef H8300_TIMER_DEFINE
#include <linux/config.h>
#if defined(CONFIG_H83007) || defined(CONFIG_H83068) || defined(CONFIG_H8S2678)
#define H8300_TIMER_COUNT_DATA CONFIG_CPU_CLOCK*10/8192
#define H8300_TIMER_FREQ CONFIG_CPU_CLOCK*1000/8192
#endif

#if defined(CONFIG_H8_3002) || defined(CONFIG_H83048)
#define H8300_TIMER_COUNT_DATA  CONFIG_CPU_CLOCK*10/8
#define H8300_TIMER_FREQ CONFIG_CPU_CLOCK*1000/8
#endif

#endif


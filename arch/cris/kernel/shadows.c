/* $Id: shadows.c,v 1.1 2000/07/10 16:25:21 bjornw Exp $
 * 
 * Various Etrax shadow registers. Defines for these are in include/asm-etrax100/io.h
 */

#include <linux/config.h>

unsigned long genconfig_shadow = 42;
unsigned long port_g_data_shadow = 42;
unsigned char port_pa_dir_shadow = 42;
unsigned char port_pa_data_shadow = 42;
unsigned char port_pb_i2c_shadow = 42;
unsigned char port_pb_config_shadow = 42;
unsigned char port_pb_dir_shadow = 42;
unsigned char port_pb_data_shadow = 42;
unsigned long r_timer_ctrl_shadow = 42;

#ifdef CONFIG_ETRAX_90000000_LEDS
unsigned long port_90000000_shadow = 42;
#endif

#ifndef _ASM_CRIS_IO_H
#define _ASM_CRIS_IO_H

#include <asm/page.h>   /* for __va, __pa */
#include <asm/svinto.h>
#include <linux/config.h>

/* Console I/O for simulated etrax100.  Use #ifdef so erroneous
   use will be evident. */
#ifdef CONFIG_SVINTO_SIM
  /* Let's use the ucsim interface since it lets us do write(2, ...) */
#define SIMCOUT(s,len) asm ("moveq 4,r1\n\tmoveq 2,r10\n\tmove.d %0,r11\n\tmove.d %1,r12\
\n\tpush irp\n\t.word 0xae3f\n\t.dword 0f\n\tjump -6809\n0:\n\tpop irp" \
       : : "rm" (s), "rm" (len) : "r1","r10","r11","r12","memory")
#define TRACE_ON() __extension__ \
 ({ int _Foofoo; __asm__ volatile ("bmod [%0],%0" : "=r" (_Foofoo) : "0" \
			       (255)); _Foofoo; })

#define TRACE_OFF() do { __asm__ volatile ("bmod [%0],%0" :: "r" (254)); } while (0)
#define SIM_END() do { __asm__ volatile ("bmod [%0],%0" :: "r" (28)); } while (0)
#define CRIS_CYCLES() __extension__ \
 ({ unsigned long c; asm ("bmod [%1],%0" : "=r" (c) : "r" (27)); c;})
#else  /* ! defined CONFIG_SVINTO_SIM */
/* FIXME: Is there a reliable cycle counter available in some chip?  Use
   that then. */
#define CRIS_CYCLES() 0
#endif /* ! defined CONFIG_SVINTO_SIM */

/* Etrax shadow registers - which live in arch/cris/kernel/shadows.c */

extern unsigned long port_g_data_shadow;
extern unsigned char port_pa_dir_shadow;
extern unsigned char port_pa_data_shadow;
extern unsigned char port_pb_i2c_shadow;
extern unsigned char port_pb_config_shadow;
extern unsigned char port_pb_dir_shadow;
extern unsigned char port_pb_data_shadow;
extern unsigned long r_timer_ctrl_shadow;
extern unsigned long port_90000000_shadow;

/* macro for setting regs through a shadow - 
 * r = register name (like R_PORT_PA_DATA)
 * s = shadow name (like port_pa_data_shadow)
 * b = bit number
 * v = value (0 or 1)
 */

#define REG_SHADOW_SET(r,s,b,v) *r = s = (s & ~(1 << b)) | ((v) << b) 

/* The LED's on various Etrax-based products are set differently. */

#if defined(CONFIG_ETRAX_NO_LEDS) || defined(CONFIG_SVINTO_SIM)
#undef CONFIG_ETRAX_PA_LEDS
#undef CONFIG_ETRAX_PB_LEDS
#undef CONFIG_ETRAX_90000000_LEDS
#define LED_NETWORK_RX_SET(x)
#define LED_NETWORK_TX_SET(x)
#define LED_ACTIVE_SET(x)
#define LED_ACTIVE_SET_G(x)
#define LED_ACTIVE_SET_R(x)
#define LED_DISK_WRITE(x)
#define LED_DISK_READ(x)
#endif

#ifdef CONFIG_ETRAX_PA_LEDS
#define LED_NETWORK_RX_SET(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED1G, !x)
#define LED_NETWORK_TX_SET(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED1R, !x)
#define LED_ACTIVE_SET(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED2G, !x)
#define LED_ACTIVE_SET_G(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED2G, !x)
#define LED_ACTIVE_SET_R(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED2R, !x)
#define LED_DISK_WRITE(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED3R, !x)
#define LED_DISK_READ(x) \
         REG_SHADOW_SET(R_PORT_PA_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED3G, !x) 
#endif

#ifdef CONFIG_ETRAX_PB_LEDS
#define LED_NETWORK_RX_SET(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED1G, !x)
#define LED_NETWORK_TX_SET(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED1R, !x)
#define LED_ACTIVE_SET(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED2G, !x)
#define LED_ACTIVE_SET_G(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED2G, !x)
#define LED_ACTIVE_SET_R(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, CONFIG_ETRAX_LED2R, !x)
#define LED_DISK_WRITE(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED3R, !x)
#define LED_DISK_READ(x) \
         REG_SHADOW_SET(R_PORT_PB_DATA, port_pa_data_shadow, CONFIG_ETRAX_LED3G, !x)     
#endif

#ifdef CONFIG_ETRAX_90000000_LEDS
/* TODO: this won't work, need a vremap into kernel virtual memory of 90000000 */
#define LED_PORT_90 (volatile unsigned long*)0x90000000
#define LED_ACTIVE_SET(x) \
         REG_SHADOW_SET(LED_PORT_90, port_90000000_shadow, CONFIG_ETRAX_LED2G, !x)
#define LED_NETWORK_RX_SET(x) \
         REG_SHADOW_SET(LED_PORT_90, port_90000000_shadow, CONFIG_ETRAX_LED1G, !x)
#define LED_NETWORK_TX_SET(x) \
         REG_SHADOW_SET(LED_PORT_90, port_90000000_shadow, CONFIG_ETRAX_LED1R, !x)
#define LED_ACTIVE_SET_G(x) \
         REG_SHADOW_SET(LED_PORT_90, port_90000000_shadow, CONFIG_ETRAX_LED2G, !x)
#define LED_ACTIVE_SET_R(x) \
         REG_SHADOW_SET(LED_PORT_90, port_90000000_shadow, CONFIG_ETRAX_LED2R, !x)
#define LED_DISK_WRITE(x) \
         REG_SHADOW_SET(LED_PORT_90, port_90000000_shadow, CONFIG_ETRAX_LED3R, !x)
#define LED_DISK_READ(x) \
         REG_SHADOW_SET(LED_PORT_90, port_90000000_shadow, CONFIG_ETRAX_LED3G, !x)           
#endif

/*
 * Change virtual addresses to physical addresses and vv.
 */

static inline unsigned long virt_to_phys(volatile void * address)
{
	return __pa(address);
}

static inline void * phys_to_virt(unsigned long address)
{
	return __va(address);
}

/*
 * IO bus memory addresses are also 1:1 with the physical address
 */
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the CRIS architecture, we just read/write the
 * memory location directly.
 */
#define readb(addr) (*(volatile unsigned char *) (addr))
#define readw(addr) (*(volatile unsigned short *) (addr))
#define readl(addr) (*(volatile unsigned int *) (addr))

#define writeb(b,addr) ((*(volatile unsigned char *) (addr)) = (b))
#define writew(b,addr) ((*(volatile unsigned short *) (addr)) = (b))
#define writel(b,addr) ((*(volatile unsigned int *) (addr)) = (b))

#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

/*
 * Again, CRIS does not require mem IO specific function.
 */

#define eth_io_copy_and_sum(a,b,c,d)	eth_copy_and_sum((a),(void *)(b),(c),(d))

/* The following is junk needed for the arch-independant code but which
 * we never use in the CRIS port
 */

#define IO_SPACE_LIMIT 0xffff
#define inb(x) (0)
#define outb(x,y)
#define outw(x,y)
#define outl(x,y)
#define insb(x,y,z)
#define insw(x,y,z)
#define insl(x,y,z)
#define outsb(x,y,z)
#define outsw(x,y,z)
#define outsl(x,y,z)

#endif

/*
 * drivers/pcmcia/sa1100_flexanet.c
 *
 * PCMCIA implementation routines for Flexanet.
 * by Jordi Colomer, 09/05/2001
 *
 * Yet to be defined.
 */

#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/pcmcia.h>


/*
 * Socket initialization.
 *
 * Called by sa1100_pcmcia_driver_init on startup.
 * Must return the number of slots.
 *
 */
static int flexanet_pcmcia_init(struct pcmcia_init *init){

  return 0;
}


/*
 * Socket shutdown
 *
 */
static int flexanet_pcmcia_shutdown(void)
{
  return 0;
}


/*
 * Get the state of the sockets.
 *
 *  Sockets in Flexanet are 3.3V only, without BVD2.
 *
 */
static int flexanet_pcmcia_socket_state(struct pcmcia_state_array
				       *state_array){
  return -1;
}


/*
 * Return the IRQ information for a given socket number (the IRQ number)
 *
 */
static int flexanet_pcmcia_get_irq_info(struct pcmcia_irq_info *info){

  return -1;
}


/*
 *
 */
static int flexanet_pcmcia_configure_socket(const struct pcmcia_configure
					   *configure)
{
  return -1;
}


/*
 * The set of socket operations
 *
 */
struct pcmcia_low_level flexanet_pcmcia_ops = {
  flexanet_pcmcia_init,
  flexanet_pcmcia_shutdown,
  flexanet_pcmcia_socket_state,
  flexanet_pcmcia_get_irq_info,
  flexanet_pcmcia_configure_socket
};


/*======================================================================

    Device driver for the PCMCIA control functionality of StrongARM
    SA-1100 microprocessors.

    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is John G. Dorsey
    <john+@cs.cmu.edu>.  Portions created by John G. Dorsey are
    Copyright (C) 1999 John G. Dorsey.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
======================================================================*/
/*
 * Please see linux/Documentation/arm/SA1100/PCMCIA for more information
 * on the low-level kernel interface.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/version.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/ss.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/arch/assabet.h>

#include "sa1100.h"

#ifdef PCMCIA_DEBUG
static int pc_debug;
#endif

/* This structure maintains housekeeping state for each socket, such
 * as the last known values of the card detect pins, or the Card Services
 * callback value associated with the socket:
 */
static int sa1100_pcmcia_socket_count;
static struct sa1100_pcmcia_socket sa1100_pcmcia_socket[SA1100_PCMCIA_MAX_SOCK];

#define PCMCIA_SOCKET(x)	(sa1100_pcmcia_socket + (x))

/* Returned by the low-level PCMCIA interface: */
static struct pcmcia_low_level *pcmcia_low_level;

static struct timer_list poll_timer;
static struct work_struct sa1100_pcmcia_task;

/*
 * sa1100_pcmcia_default_mecr_timing
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * Calculate MECR clock wait states for given CPU clock
 * speed and command wait state. This function can be over-
 * written by a board specific version.
 *
 * The default is to simply calculate the BS values as specified in
 * the INTEL SA1100 development manual
 * "Expansion Memory (PCMCIA) Configuration Register (MECR)"
 * that's section 10.2.5 in _my_ version of the manuial ;)
 */
static unsigned int
sa1100_pcmcia_default_mecr_timing(unsigned int sock, unsigned int cpu_speed,
				  unsigned int cmd_time)
{
	return sa1100_pcmcia_mecr_bs(cmd_time, cpu_speed);
}

/* sa1100_pcmcia_set_mecr()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * set MECR value for socket <sock> based on this sockets
 * io, mem and attribute space access speed.
 * Call board specific BS value calculation to allow boards
 * to tweak the BS values.
 */
static int
sa1100_pcmcia_set_mecr(struct sa1100_pcmcia_socket *skt, unsigned int cpu_clock)
{
	u32 mecr;
	unsigned long flags;
	unsigned int bs;

	local_irq_save(flags);

	bs = skt->ops->socket_get_timing(skt->nr, cpu_clock, skt->speed_io);

	mecr = MECR;
	MECR_FAST_SET(mecr, skt->nr, 0);
	MECR_BSIO_SET(mecr, skt->nr, bs );
	MECR_BSA_SET(mecr, skt->nr, bs );
	MECR_BSM_SET(mecr, skt->nr, bs );
	MECR = mecr;

	local_irq_restore(flags);

	DEBUG(4, "%s(): sock %u FAST %X  BSM %X  BSA %X  BSIO %X\n",
	      __FUNCTION__, skt->nr, MECR_FAST_GET(mecr, skt->nr),
	      MECR_BSM_GET(mecr, skt->nr), MECR_BSA_GET(mecr, skt->nr),
	      MECR_BSIO_GET(mecr, skt->nr));

	return 0;
}

/*
 * sa1100_pcmcia_config_skt
 * ^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * Convert PCMCIA socket state to our socket configure structure.
 */
static int
sa1100_pcmcia_config_skt(struct sa1100_pcmcia_socket *skt, socket_state_t *state)
{
	struct pcmcia_configure conf;
	int ret;

	conf.vcc     = state->Vcc;
	conf.vpp     = state->Vpp;
	conf.output  = state->flags & SS_OUTPUT_ENA ? 1 : 0;
	conf.speaker = state->flags & SS_SPKR_ENA ? 1 : 0;
	conf.reset   = state->flags & SS_RESET ? 1 : 0;

	ret = skt->ops->configure_socket(skt->nr, &conf);
	if (ret == 0) {
		/*
		 * This really needs a better solution.  The IRQ
		 * may or may not be claimed by the driver.
		 */
		if (skt->irq_state != 1 && state->io_irq) {
			skt->irq_state = 1;
			set_irq_type(skt->irq, IRQT_FALLING);
		} else if (skt->irq_state == 1 && state->io_irq == 0) {
		  	skt->irq_state = 0;
			set_irq_type(skt->irq, IRQT_NOEDGE);
		}

		skt->cs_state = *state;
	}

	if (ret < 0)
		printk(KERN_ERR "sa1100_pcmcia: unable to configure "
		       "socket %d\n", skt->nr);

	return ret;
}

/* sa1100_pcmcia_sock_init()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * (Re-)Initialise the socket, turning on status interrupts
 * and PCMCIA bus.  This must wait for power to stabilise
 * so that the card status signals report correctly.
 *
 * Returns: 0
 */
static int sa1100_pcmcia_sock_init(unsigned int sock)
{
  struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(sock);

  DEBUG(2, "%s(): initializing socket %u\n", __FUNCTION__, sock);

  sa1100_pcmcia_config_skt(skt, &dead_socket);

  return skt->ops->socket_init(skt->nr);
}


/*
 * sa1100_pcmcia_suspend()
 * ^^^^^^^^^^^^^^^^^^^^^^^
 *
 * Remove power on the socket, disable IRQs from the card.
 * Turn off status interrupts, and disable the PCMCIA bus.
 *
 * Returns: 0
 */
static int sa1100_pcmcia_suspend(unsigned int sock)
{
  struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(sock);
  int ret;

  DEBUG(2, "%s(): suspending socket %u\n", __FUNCTION__, skt->nr);

  ret = sa1100_pcmcia_config_skt(skt, &dead_socket);

  if (ret == 0)
    ret = skt->ops->socket_suspend(skt->nr);

  return ret;
}


/* sa1100_pcmcia_events()
 * ^^^^^^^^^^^^^^^^^^^^^^
 * Helper routine to generate a Card Services event mask based on
 * state information obtained from the kernel low-level PCMCIA layer
 * in a recent (and previous) sampling. Updates `prev_state'.
 *
 * Returns: an event mask for the given socket state.
 */
static inline unsigned int
sa1100_pcmcia_events(struct pcmcia_state *state,
		     struct pcmcia_state *prev_state,
		     unsigned int mask, unsigned int flags)
{
  unsigned int events = 0;

  if (state->detect != prev_state->detect) {
    DEBUG(3, "%s(): card detect value %u\n", __FUNCTION__, state->detect);

    events |= SS_DETECT;
  }

  if (state->ready != prev_state->ready) {
    DEBUG(3, "%s(): card ready value %u\n", __FUNCTION__, state->ready);

    events |= flags & SS_IOCARD ? 0 : SS_READY;
  }

  if (state->bvd1 != prev_state->bvd1) {
    DEBUG(3, "%s(): card BVD1 value %u\n", __FUNCTION__, state->bvd1);

    events |= flags & SS_IOCARD ? SS_STSCHG : SS_BATDEAD;
  }

  if (state->bvd2 != prev_state->bvd2) {
    DEBUG(3, "%s(): card BVD2 value %u\n", __FUNCTION__, state->bvd2);

    events |= flags & SS_IOCARD ? 0 : SS_BATWARN;
  }

  *prev_state = *state;

  events &= mask;

  DEBUG(2, "events: %s%s%s%s%s%s\n",
	events == 0         ? "<NONE>"   : "",
	events & SS_DETECT  ? "DETECT "  : "",
	events & SS_READY   ? "READY "   : "",
	events & SS_BATDEAD ? "BATDEAD " : "",
	events & SS_BATWARN ? "BATWARN " : "",
	events & SS_STSCHG  ? "STSCHG "  : "");

  return events;
}  /* sa1100_pcmcia_events() */


/* sa1100_pcmcia_task_handler()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Processes serviceable socket events using the "eventd" thread context.
 *
 * Event processing (specifically, the invocation of the Card Services event
 * callback) occurs in this thread rather than in the actual interrupt
 * handler due to the use of scheduling operations in the PCMCIA core.
 */
static void sa1100_pcmcia_task_handler(void *data)
{
  struct pcmcia_state state;
  unsigned int all_events;

  DEBUG(4, "%s(): entering PCMCIA monitoring thread\n", __FUNCTION__);

  do {
    unsigned int events;
    int i;

    DEBUG(4, "%s(): interrogating low-level PCMCIA service\n", __FUNCTION__);

    all_events = 0;

    for (i = 0; i < sa1100_pcmcia_socket_count; i++) {
      struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(i);

      memset(&state, 0, sizeof(state));

      skt->ops->socket_state(skt->nr, &state);

      events = sa1100_pcmcia_events(&state, &skt->k_state,
				    skt->cs_state.csc_mask,
				    skt->cs_state.flags);

      if (events && skt->handler != NULL)
	skt->handler(skt->handler_info, events);
      all_events |= events;
    }
  } while(all_events);
}  /* sa1100_pcmcia_task_handler() */

static DECLARE_WORK(sa1100_pcmcia_task, sa1100_pcmcia_task_handler, NULL);


/* sa1100_pcmcia_poll_event()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Let's poll for events in addition to IRQs since IRQ only is unreliable...
 */
static void sa1100_pcmcia_poll_event(unsigned long dummy)
{
  DEBUG(4, "%s(): polling for events\n", __FUNCTION__);
  init_timer(&poll_timer);
  poll_timer.function = sa1100_pcmcia_poll_event;
  poll_timer.expires = jiffies + SA1100_PCMCIA_POLL_PERIOD;
  add_timer(&poll_timer);
  schedule_work(&sa1100_pcmcia_task);
}


/* sa1100_pcmcia_interrupt()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^
 * Service routine for socket driver interrupts (requested by the
 * low-level PCMCIA init() operation via sa1100_pcmcia_thread()).
 * The actual interrupt-servicing work is performed by
 * sa1100_pcmcia_thread(), largely because the Card Services event-
 * handling code performs scheduling operations which cannot be
 * executed from within an interrupt context.
 */
void sa1100_pcmcia_interrupt(int irq, void *dev, struct pt_regs *regs)
{
  DEBUG(3, "%s(): servicing IRQ %d\n", __FUNCTION__, irq);
  schedule_work(&sa1100_pcmcia_task);
}


/* sa1100_pcmcia_register_callback()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Implements the register_callback() operation for the in-kernel
 * PCMCIA service (formerly SS_RegisterCallback in Card Services). If 
 * the function pointer `handler' is not NULL, remember the callback 
 * location in the state for `sock', and increment the usage counter 
 * for the driver module. (The callback is invoked from the interrupt
 * service routine, sa1100_pcmcia_interrupt(), to notify Card Services
 * of interesting events.) Otherwise, clear the callback pointer in the
 * socket state and decrement the module usage count.
 *
 * Returns: 0
 */
static int
sa1100_pcmcia_register_callback(unsigned int sock,
				void (*handler)(void *, unsigned int),
				void *info)
{
  struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(sock);

  if (handler && !try_module_get(skt->ops->owner))
  	return -ENODEV;
  if (handler == NULL) {
    skt->handler = NULL;
  } else {
    skt->handler_info = info;
    skt->handler = handler;
  }
  if (!handler)
  	module_put(skt->ops->owner);

  return 0;
}


/* sa1100_pcmcia_inquire_socket()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Implements the inquire_socket() operation for the in-kernel PCMCIA
 * service (formerly SS_InquireSocket in Card Services). Of note is
 * the setting of the SS_CAP_PAGE_REGS bit in the `features' field of
 * `cap' to "trick" Card Services into tolerating large "I/O memory" 
 * addresses. Also set is SS_CAP_STATIC_MAP, which disables the memory
 * resource database check. (Mapped memory is set up within the socket
 * driver itself.)
 *
 * In conjunction with the STATIC_MAP capability is a new field,
 * `io_offset', recommended by David Hinds. Rather than go through
 * the SetIOMap interface (which is not quite suited for communicating
 * window locations up from the socket driver), we just pass up
 * an offset which is applied to client-requested base I/O addresses
 * in alloc_io_space().
 *
 * SS_CAP_PAGE_REGS: used by setup_cis_mem() in cistpl.c to set the
 *   force_low argument to validate_mem() in rsrc_mgr.c -- since in
 *   general, the mapped * addresses of the PCMCIA memory regions
 *   will not be within 0xffff, setting force_low would be
 *   undesirable.
 *
 * SS_CAP_STATIC_MAP: don't bother with the (user-configured) memory
 *   resource database; we instead pass up physical address ranges
 *   and allow other parts of Card Services to deal with remapping.
 *
 * SS_CAP_PCCARD: we can deal with 16-bit PCMCIA & CF cards, but
 *   not 32-bit CardBus devices.
 *
 * Return value is irrelevant; the pcmcia subsystem ignores it.
 */
static int
sa1100_pcmcia_inquire_socket(unsigned int sock, socket_cap_t *cap)
{
  struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(sock);
  int ret = -1;

  DEBUG(2, "%s() for sock %u\n", __FUNCTION__, skt->nr);

  if (sock < sa1100_pcmcia_socket_count) {
    cap->features  = SS_CAP_PAGE_REGS | SS_CAP_STATIC_MAP | SS_CAP_PCCARD;
    cap->irq_mask  = 0;
    cap->map_size  = PAGE_SIZE;
    cap->pci_irq   = skt->irq;
    cap->io_offset = (unsigned long)skt->virt_io;

    ret = 0;
  }

  return ret;
}


/* sa1100_pcmcia_get_status()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Implements the get_status() operation for the in-kernel PCMCIA
 * service (formerly SS_GetStatus in Card Services). Essentially just
 * fills in bits in `status' according to internal driver state or
 * the value of the voltage detect chipselect register.
 *
 * As a debugging note, during card startup, the PCMCIA core issues
 * three set_socket() commands in a row the first with RESET deasserted,
 * the second with RESET asserted, and the last with RESET deasserted
 * again. Following the third set_socket(), a get_status() command will
 * be issued. The kernel is looking for the SS_READY flag (see
 * setup_socket(), reset_socket(), and unreset_socket() in cs.c).
 *
 * Returns: 0
 */
static int
sa1100_pcmcia_get_status(unsigned int sock, unsigned int *status)
{
  struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(sock);
  struct pcmcia_state state;
  unsigned int stat;

  DEBUG(2, "%s() for sock %u\n", __FUNCTION__, skt->nr);

  memset(&state, 0, sizeof(state));

  skt->ops->socket_state(skt->nr, &state);
  skt->k_state = state;

  stat = state.detect ? SS_DETECT : 0;
  stat |= state.ready ? SS_READY  : 0;
  stat |= state.vs_3v ? SS_3VCARD : 0;
  stat |= state.vs_Xv ? SS_XVCARD : 0;

  /* The power status of individual sockets is not available
   * explicitly from the hardware, so we just remember the state
   * and regurgitate it upon request:
   */
  stat |= skt->cs_state.Vcc ? SS_POWERON : 0;

  if (skt->cs_state.flags & SS_IOCARD)
    stat |= state.bvd1 ? SS_STSCHG : 0;
  else {
    if (state.bvd1 == 0)
      stat |= SS_BATDEAD;
    else if (state.bvd2 == 0)
      stat |= SS_BATWARN;
  }

  DEBUG(3, "\tstatus: %s%s%s%s%s%s%s%s\n",
	stat & SS_DETECT  ? "DETECT "  : "",
	stat & SS_READY   ? "READY "   : "", 
	stat & SS_BATDEAD ? "BATDEAD " : "",
	stat & SS_BATWARN ? "BATWARN " : "",
	stat & SS_POWERON ? "POWERON " : "",
	stat & SS_STSCHG  ? "STSCHG "  : "",
	stat & SS_3VCARD  ? "3VCARD "  : "",
	stat & SS_XVCARD  ? "XVCARD "  : "");

  *status = stat;

  return 0;
}  /* sa1100_pcmcia_get_status() */


/* sa1100_pcmcia_get_socket()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Implements the get_socket() operation for the in-kernel PCMCIA
 * service (formerly SS_GetSocket in Card Services). Not a very 
 * exciting routine.
 *
 * Returns: 0
 */
static int
sa1100_pcmcia_get_socket(unsigned int sock, socket_state_t *state)
{
  struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(sock);

  DEBUG(2, "%s() for sock %u\n", __FUNCTION__, skt->nr);

  *state = skt->cs_state;

  return 0;
}

/* sa1100_pcmcia_set_socket()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Implements the set_socket() operation for the in-kernel PCMCIA
 * service (formerly SS_SetSocket in Card Services). We more or
 * less punt all of this work and let the kernel handle the details
 * of power configuration, reset, &c. We also record the value of
 * `state' in order to regurgitate it to the PCMCIA core later.
 *
 * Returns: 0
 */
static int
sa1100_pcmcia_set_socket(unsigned int sock, socket_state_t *state)
{
  struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(sock);

  DEBUG(2, "%s() for sock %u\n", __FUNCTION__, skt->nr);

  DEBUG(3, "\tmask:  %s%s%s%s%s%s\n\tflags: %s%s%s%s%s%s\n",
	(state->csc_mask==0)?"<NONE>":"",
	(state->csc_mask&SS_DETECT)?"DETECT ":"",
	(state->csc_mask&SS_READY)?"READY ":"",
	(state->csc_mask&SS_BATDEAD)?"BATDEAD ":"",
	(state->csc_mask&SS_BATWARN)?"BATWARN ":"",
	(state->csc_mask&SS_STSCHG)?"STSCHG ":"",
	(state->flags==0)?"<NONE>":"",
	(state->flags&SS_PWR_AUTO)?"PWR_AUTO ":"",
	(state->flags&SS_IOCARD)?"IOCARD ":"",
	(state->flags&SS_RESET)?"RESET ":"",
	(state->flags&SS_SPKR_ENA)?"SPKR_ENA ":"",
	(state->flags&SS_OUTPUT_ENA)?"OUTPUT_ENA ":"");
  DEBUG(3, "\tVcc %d  Vpp %d  irq %d\n",
	state->Vcc, state->Vpp, state->io_irq);

  return sa1100_pcmcia_config_skt(skt, state);
}  /* sa1100_pcmcia_set_socket() */


/* sa1100_pcmcia_set_io_map()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Implements the set_io_map() operation for the in-kernel PCMCIA
 * service (formerly SS_SetIOMap in Card Services). We configure
 * the map speed as requested, but override the address ranges
 * supplied by Card Services.
 *
 * Returns: 0 on success, -1 on error
 */
static int
sa1100_pcmcia_set_io_map(unsigned int sock, struct pccard_io_map *map)
{
  struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(sock);

  DEBUG(2, "%s() for sock %u\n", __FUNCTION__, skt->nr);

  DEBUG(3, "\tmap %u  speed %u\n\tstart 0x%08x  stop 0x%08x\n",
	map->map, map->speed, map->start, map->stop);
  DEBUG(3, "\tflags: %s%s%s%s%s%s%s%s\n",
	(map->flags==0)?"<NONE>":"",
	(map->flags&MAP_ACTIVE)?"ACTIVE ":"",
	(map->flags&MAP_16BIT)?"16BIT ":"",
	(map->flags&MAP_AUTOSZ)?"AUTOSZ ":"",
	(map->flags&MAP_0WS)?"0WS ":"",
	(map->flags&MAP_WRPROT)?"WRPROT ":"",
	(map->flags&MAP_USE_WAIT)?"USE_WAIT ":"",
	(map->flags&MAP_PREFETCH)?"PREFETCH ":"");

  if (map->map >= MAX_IO_WIN) {
    printk(KERN_ERR "%s(): map (%d) out of range\n", __FUNCTION__,
	   map->map);
    return -1;
  }

  if (map->flags & MAP_ACTIVE) {
    if ( map->speed == 0)
       map->speed = SA1100_PCMCIA_IO_ACCESS;

	sa1100_pcmcia_set_mecr(skt, cpufreq_get(0));
  }

  if (map->stop == 1)
    map->stop = PAGE_SIZE-1;

  map->stop -= map->start;
  map->stop += (unsigned long)skt->virt_io;
  map->start = (unsigned long)skt->virt_io;

  skt->io_map[map->map] = *map;

  return 0;
}  /* sa1100_pcmcia_set_io_map() */


/* sa1100_pcmcia_set_mem_map()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Implements the set_mem_map() operation for the in-kernel PCMCIA
 * service (formerly SS_SetMemMap in Card Services). We configure
 * the map speed as requested, but override the address ranges
 * supplied by Card Services.
 *
 * Returns: 0 on success, -1 on error
 */
static int
sa1100_pcmcia_set_mem_map(unsigned int sock, struct pccard_mem_map *map)
{
  struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(sock);
  unsigned long start;

  DEBUG(2, "%s() for sock %u\n", __FUNCTION__, skt->nr);

  DEBUG(3, "\tmap %u speed %u sys_start %08lx sys_stop %08lx card_start %08x\n",
	map->map, map->speed, map->sys_start, map->sys_stop, map->card_start);
  DEBUG(3, "\tflags: %s%s%s%s%s%s%s%s\n",
	(map->flags==0)?"<NONE>":"",
	(map->flags&MAP_ACTIVE)?"ACTIVE ":"",
	(map->flags&MAP_16BIT)?"16BIT ":"",
	(map->flags&MAP_AUTOSZ)?"AUTOSZ ":"",
	(map->flags&MAP_0WS)?"0WS ":"",
	(map->flags&MAP_WRPROT)?"WRPROT ":"",
	(map->flags&MAP_ATTRIB)?"ATTRIB ":"",
	(map->flags&MAP_USE_WAIT)?"USE_WAIT ":"");

  if (map->map >= MAX_WIN) {
    printk(KERN_ERR "%s(): map (%d) out of range\n", __FUNCTION__,
	   map->map);
    return -1;
  }

  if (map->flags & MAP_ACTIVE) {
	  /*
	   * When clients issue RequestMap, the access speed is not always
	   * properly configured.  Choose some sensible defaults.
	   */
	  if (map->speed == 0) {
		  if (skt->cs_state.Vcc == 33)
			  map->speed = SA1100_PCMCIA_3V_MEM_ACCESS;
		  else
			  map->speed = SA1100_PCMCIA_5V_MEM_ACCESS;
	  }

	  sa1100_pcmcia_set_mecr(skt, cpufreq_get(0));

  }

  if (map->sys_stop == 0)
    map->sys_stop = PAGE_SIZE-1;

  start = (map->flags & MAP_ATTRIB) ? skt->phys_attr : skt->phys_mem;
  map->sys_stop -= map->sys_start;
  map->sys_stop += start + map->card_start;
  map->sys_start = start + map->card_start;

  skt->pc_mem_map[map->map] = *map;

  return 0;
}  /* sa1100_pcmcia_set_mem_map() */


#if defined(CONFIG_PROC_FS)

/* sa1100_pcmcia_proc_status()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Implements the /proc/bus/pccard/??/status file.
 *
 * Returns: the number of characters added to the buffer
 */
static int
sa1100_pcmcia_proc_status(char *buf, char **start, off_t pos,
			  int count, int *eof, void *data)
{
  struct sa1100_pcmcia_socket *skt = data;
  unsigned int clock = cpufreq_get(0);
  unsigned long mecr = MECR;
  char *p = buf;

  p+=sprintf(p, "k_state  : %s%s%s%s%s%s%s\n", 
	     skt->k_state.detect ? "detect " : "",
	     skt->k_state.ready  ? "ready "  : "",
	     skt->k_state.bvd1   ? "bvd1 "   : "",
	     skt->k_state.bvd2   ? "bvd2 "   : "",
	     skt->k_state.wrprot ? "wrprot " : "",
	     skt->k_state.vs_3v  ? "vs_3v "  : "",
	     skt->k_state.vs_Xv  ? "vs_Xv "  : "");

  p+=sprintf(p, "status   : %s%s%s%s%s%s%s%s%s\n",
	     skt->k_state.detect ? "SS_DETECT " : "",
	     skt->k_state.ready  ? "SS_READY " : "",
	     skt->cs_state.Vcc   ? "SS_POWERON " : "",
	     skt->cs_state.flags & SS_IOCARD ? "SS_IOCARD " : "",
	     (skt->cs_state.flags & SS_IOCARD &&
	      skt->k_state.bvd1) ? "SS_STSCHG " : "",
	     ((skt->cs_state.flags & SS_IOCARD)==0 &&
	      (skt->k_state.bvd1==0)) ? "SS_BATDEAD " : "",
	     ((skt->cs_state.flags & SS_IOCARD)==0 &&
	      (skt->k_state.bvd2==0)) ? "SS_BATWARN " : "",
	     skt->k_state.vs_3v  ? "SS_3VCARD " : "",
	     skt->k_state.vs_Xv  ? "SS_XVCARD " : "");

  p+=sprintf(p, "mask     : %s%s%s%s%s\n",
	     skt->cs_state.csc_mask & SS_DETECT  ? "SS_DETECT "  : "",
	     skt->cs_state.csc_mask & SS_READY   ? "SS_READY "   : "",
	     skt->cs_state.csc_mask & SS_BATDEAD ? "SS_BATDEAD " : "",
	     skt->cs_state.csc_mask & SS_BATWARN ? "SS_BATWARN " : "",
	     skt->cs_state.csc_mask & SS_STSCHG  ? "SS_STSCHG "  : "");

  p+=sprintf(p, "cs_flags : %s%s%s%s%s\n",
	     skt->cs_state.flags & SS_PWR_AUTO   ? "SS_PWR_AUTO "   : "",
	     skt->cs_state.flags & SS_IOCARD     ? "SS_IOCARD "     : "",
	     skt->cs_state.flags & SS_RESET      ? "SS_RESET "      : "",
	     skt->cs_state.flags & SS_SPKR_ENA   ? "SS_SPKR_ENA "   : "",
	     skt->cs_state.flags & SS_OUTPUT_ENA ? "SS_OUTPUT_ENA " : "");

  p+=sprintf(p, "Vcc      : %d\n", skt->cs_state.Vcc);
  p+=sprintf(p, "Vpp      : %d\n", skt->cs_state.Vpp);
  p+=sprintf(p, "IRQ      : %d\n", skt->cs_state.io_irq);

  p+=sprintf(p, "I/O      : %u (%u)\n", skt->speed_io,
	     sa1100_pcmcia_cmd_time(clock, MECR_BSIO_GET(mecr, skt->nr)));

  p+=sprintf(p, "attribute: %u (%u)\n", skt->speed_attr,
	     sa1100_pcmcia_cmd_time(clock, MECR_BSA_GET(mecr, skt->nr)));

  p+=sprintf(p, "common   : %u (%u)\n", skt->speed_mem,
	     sa1100_pcmcia_cmd_time(clock, MECR_BSM_GET(mecr, skt->nr)));

  return p-buf;
}

/* sa1100_pcmcia_proc_setup()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Implements the proc_setup() operation for the in-kernel PCMCIA
 * service (formerly SS_ProcSetup in Card Services).
 *
 * Returns: 0 on success, -1 on error
 */
static void
sa1100_pcmcia_proc_setup(unsigned int sock, struct proc_dir_entry *base)
{
  struct proc_dir_entry *entry;

  DEBUG(4, "%s() for sock %u\n", __FUNCTION__, sock);

  if ((entry = create_proc_entry("status", 0, base)) == NULL){
    printk(KERN_ERR "unable to install \"status\" procfs entry\n");
    return;
  }

  entry->read_proc = sa1100_pcmcia_proc_status;
  entry->data = PCMCIA_SOCKET(sock);
}

#endif  /* defined(CONFIG_PROC_FS) */

static struct pccard_operations sa1100_pcmcia_operations = {
  .owner		= THIS_MODULE,
  .init			= sa1100_pcmcia_sock_init,
  .suspend		= sa1100_pcmcia_suspend,
  .register_callback	= sa1100_pcmcia_register_callback,
  .inquire_socket	= sa1100_pcmcia_inquire_socket,
  .get_status		= sa1100_pcmcia_get_status,
  .get_socket		= sa1100_pcmcia_get_socket,
  .set_socket		= sa1100_pcmcia_set_socket,
  .set_io_map		= sa1100_pcmcia_set_io_map,
  .set_mem_map		= sa1100_pcmcia_set_mem_map,
#ifdef CONFIG_PROC_FS
  .proc_setup		= sa1100_pcmcia_proc_setup
#endif
};

#ifdef CONFIG_CPU_FREQ

/* sa1100_pcmcia_update_mecr()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * When sa1100_pcmcia_notifier() decides that a MECR adjustment (due
 * to a core clock frequency change) is needed, this routine establishes
 * new BS_xx values consistent with the clock speed `clock'.
 */
static void sa1100_pcmcia_update_mecr(unsigned int clock)
{
	unsigned int sock;

	for (sock = 0; sock < SA1100_PCMCIA_MAX_SOCK; ++sock) {
		struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(sock);
		sa1100_pcmcia_set_mecr(skt, clock);
	}
}

/* sa1100_pcmcia_notifier()
 * ^^^^^^^^^^^^^^^^^^^^^^^^
 * When changing the processor core clock frequency, it is necessary
 * to adjust the MECR timings accordingly. We've recorded the timings
 * requested by Card Services, so this is just a matter of finding
 * out what our current speed is, and then recomputing the new MECR
 * values.
 *
 * Returns: 0 on success, -1 on error
 */
static int
sa1100_pcmcia_notifier(struct notifier_block *nb, unsigned long val,
		       void *data)
{
	struct cpufreq_freqs *freqs = data;

	switch (val) {
	case CPUFREQ_PRECHANGE:
		if (freqs->new > freqs->old) {
			DEBUG(2, "%s(): new frequency %u.%uMHz > %u.%uMHz, "
				"pre-updating\n", __FUNCTION__,
			    freqs->new / 1000, (freqs->new / 100) % 10,
			    freqs->old / 1000, (freqs->old / 100) % 10);
			sa1100_pcmcia_update_mecr(freqs->new);
		}
		break;

	case CPUFREQ_POSTCHANGE:
		if (freqs->new < freqs->old) {
			DEBUG(2, "%s(): new frequency %u.%uMHz < %u.%uMHz, "
				"post-updating\n", __FUNCTION__,
			    freqs->new / 1000, (freqs->new / 100) % 10,
			    freqs->old / 1000, (freqs->old / 100) % 10);
			sa1100_pcmcia_update_mecr(freqs->new);
		}
		break;
	}

	return 0;
}

static struct notifier_block sa1100_pcmcia_notifier_block = {
	.notifier_call	= sa1100_pcmcia_notifier
};
#endif

/* sa1100_register_pcmcia()
 * ^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * Register an SA1100 PCMCIA low level driver with the SA1100 core.
 */
int sa1100_register_pcmcia(struct pcmcia_low_level *ops, struct device *dev)
{
	struct pcmcia_init pcmcia_init;
	struct pcmcia_socket_class_data *cls;
	unsigned int i, cpu_clock;
	int ret;

	/*
	 * Refuse to replace an existing driver.
	 */
	if (pcmcia_low_level)
		return -EBUSY;

	pcmcia_low_level = ops;

	/*
	 * set default MECR calculation if the board specific
	 * code did not specify one...
	 */
	if (!ops->socket_get_timing)
		ops->socket_get_timing = sa1100_pcmcia_default_mecr_timing;

	pcmcia_init.socket_irq[0] = NO_IRQ;
	pcmcia_init.socket_irq[1] = NO_IRQ;
	ret = ops->init(&pcmcia_init);
	if (ret < 0) {
		printk(KERN_ERR "Unable to initialize kernel PCMCIA service (%d).\n", ret);
		goto out;
	}

	sa1100_pcmcia_socket_count = ret;

	cpu_clock = cpufreq_get(0);

	for (i = 0; i < sa1100_pcmcia_socket_count; i++) {
		struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(i);
		memset(skt, 0, sizeof(*skt));
	}

	/*
	 * We initialize the MECR to default values here, because we are
	 * not guaranteed to see a SetIOMap operation at runtime.
	 */
	for (i = 0; i < sa1100_pcmcia_socket_count; i++) {
		struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(i);

		skt->res.start	= _PCMCIA(i);
		skt->res.end	= _PCMCIA(i) + PCMCIASp - 1;
		skt->res.name	= "PCMCIA";
		skt->res.flags	= IORESOURCE_MEM;

		ret = request_resource(&iomem_resource, &skt->res);
		if (ret)
			goto out_err;

		skt->nr		= i;
		skt->ops	= ops;
		skt->irq	= pcmcia_init.socket_irq[i];
		skt->irq_state	= 0;
		skt->speed_io   = SA1100_PCMCIA_IO_ACCESS;
		skt->speed_attr = SA1100_PCMCIA_5V_MEM_ACCESS;
		skt->speed_mem  = SA1100_PCMCIA_5V_MEM_ACCESS;
		skt->phys_attr  = _PCMCIAAttr(i);
		skt->phys_mem   = _PCMCIAMem(i);
		skt->virt_io    = ioremap(_PCMCIAIO(i), 0x10000);

		if (skt->virt_io == NULL) {
			ret = -ENOMEM;
			goto out_err;
		}

		ops->socket_state(skt->nr, &skt->k_state);
		sa1100_pcmcia_set_mecr(skt, cpu_clock);
	}

	cls = kmalloc(sizeof(struct pcmcia_socket_class_data), GFP_KERNEL);
	if (!cls) {
		ret = -ENOMEM;
		goto out_err;
	}

	memset(cls, 0, sizeof(struct pcmcia_socket_class_data));

	cls->ops	= &sa1100_pcmcia_operations;
	cls->nsock	= sa1100_pcmcia_socket_count;
	dev->class_data = cls;

	/*
	 * Start the event poll timer.  It will reschedule by itself afterwards.
	 */
	sa1100_pcmcia_poll_event(0);
	return 0;

 out_err:
	for (i = 0; i < sa1100_pcmcia_socket_count; i++) {
		struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(i);
		iounmap(skt->virt_io);
		skt->virt_io = NULL;
		if (skt->res.start)
			release_resource(&skt->res);
	}

	ops->shutdown();

 out:
	pcmcia_low_level = NULL;
	return ret;
}
EXPORT_SYMBOL(sa1100_register_pcmcia);

/* sa1100_unregister_pcmcia()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * Unregister a previously registered pcmcia driver
 */
void sa1100_unregister_pcmcia(struct pcmcia_low_level *ops, struct device *dev)
{
	int i;

	if (!ops)
		return;

	if (ops != pcmcia_low_level) {
		printk(KERN_DEBUG "PCMCIA: Trying to unregister wrong "
			"low-level driver (%p != %p)", ops,
			pcmcia_low_level);
		return;
	}

	del_timer_sync(&poll_timer);

	for (i = 0; i < sa1100_pcmcia_socket_count; i++) {
		struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(i);

		iounmap(skt->virt_io);
		skt->virt_io = NULL;
		
		release_resource(&skt->res);
	}

	ops->shutdown();

	flush_scheduled_work();

	kfree(dev->class_data);
	dev->class_data = NULL;

	pcmcia_low_level = NULL;
}
EXPORT_SYMBOL(sa1100_unregister_pcmcia);

static struct device_driver sa1100_pcmcia_driver = {
	.name		= "sa11x0-pcmcia",
	.bus		= &platform_bus_type,
	.devclass	= &pcmcia_socket_class,
	.suspend 	= pcmcia_socket_dev_suspend,
	.resume 	= pcmcia_socket_dev_resume,
};

static struct platform_device sa1100_pcmcia_device = {
	.name		= "sa11x0-pcmcia",
	.id		= 0,
	.dev		= {
		.name	= "Intel Corporation SA11x0 [PCMCIA]",
	},
};

struct ll_fns {
	int (*init)(struct device *dev);
	void (*exit)(struct device *dev);
};

static struct ll_fns sa1100_ll_fns[] = {
#ifdef CONFIG_SA1100_ASSABET
	{ .init = pcmcia_assabet_init,	.exit = pcmcia_assabet_exit,	},
#endif
#ifdef CONFIG_SA1100_CERF
	{ .init = pcmcia_cerf_init,	.exit = pcmcia_cerf_exit,	},
#endif
#ifdef CONFIG_SA1100_FLEXANET
	{ .init = pcmcia_flexanet_init,	.exit = pcmcia_flexanet_exit,	},
#endif
#ifdef CONFIG_SA1100_FREEBIRD
	{ .init = pcmcia_freebird_init,	.exit = pcmcia_freebird_exit,	},
#endif
#ifdef CONFIG_SA1100_GRAPHICSCLIENT
	{ .init = pcmcia_gcplus_init,	.exit = pcmcia_gcplus_exit,	},
#endif
#ifdef CONFIG_SA1100_H3600
	{ .init = pcmcia_h3600_init,	.exit = pcmcia_h3600_exit,	},
#endif
#ifdef CONFIG_SA1100_PANGOLIN
	{ .init = pcmcia_pangolin_init,	.exit = pcmcia_pangolin_exit,	},
#endif
#ifdef CONFIG_SA1100_SHANNON
	{ .init = pcmcia_shannon_init,	.exit = pcmcia_shannon_exit,	},
#endif
#ifdef CONFIG_SA1100_SIMPAD
	{ .init = pcmcia_simpad_init,	.exit = pcmcia_simpad_exit,	},
#endif
#ifdef CONFIG_SA1100_STORK
	{ .init = pcmcia_stork_init,	.exit = pcmcia_stork_exit,	},
#endif
#ifdef CONFIG_SA1100_TRIZEPS
	{ .init = pcmcia_trizeps_init,	.exit = pcmcia_trizeps_exit,	},
#endif
#ifdef CONFIG_SA1100_YOPY
	{ .init = pcmcia_yopy_init,	.exit = pcmcia_yopy_exit,	},
#endif
};

/* sa1100_pcmcia_init()
 * ^^^^^^^^^^^^^^^^^^^^
 *
 * This routine performs a basic sanity check to ensure that this
 * kernel has been built with the appropriate board-specific low-level
 * PCMCIA support, performs low-level PCMCIA initialization, registers
 * this socket driver with Card Services, and then spawns the daemon
 * thread which is the real workhorse of the socket driver.
 *
 * Returns: 0 on success, -1 on error
 */
static int __init sa1100_pcmcia_init(void)
{
	servinfo_t info;
	int ret, i;

	printk(KERN_INFO "SA11x0 PCMCIA (CS release %s)\n", CS_RELEASE);

	CardServices(GetCardServicesInfo, &info);
	if (info.Revision != CS_RELEASE_CODE) {
		printk(KERN_ERR "Card Services release codes do not match\n");
		return -EINVAL;
	}

#ifdef CONFIG_CPU_FREQ
	ret = cpufreq_register_notifier(&sa1100_pcmcia_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
	if (ret < 0) {
		printk(KERN_ERR "Unable to register CPU frequency change "
			"notifier (%d)\n", ret);
		driver_unregister(&sa1100_pcmcia_driver);
		return ret;
	}
#endif

	driver_register(&sa1100_pcmcia_driver);

	/*
	 * Initialise any "on-board" PCMCIA sockets.
	 */
	for (i = 0; i < ARRAY_SIZE(sa1100_ll_fns); i++) {
		ret = sa1100_ll_fns[i].init(&sa1100_pcmcia_device.dev);
		if (ret == 0)
			break;
	}

	if (ret == 0)
		platform_device_register(&sa1100_pcmcia_device);

	/*
	 * Don't fail if we don't find any on-board sockets.
	 */
	return 0;
}

/* sa1100_pcmcia_exit()
 * ^^^^^^^^^^^^^^^^^^^^
 * Invokes the low-level kernel service to free IRQs associated with this
 * socket controller and reset GPIO edge detection.
 */
static void __exit sa1100_pcmcia_exit(void)
{
	platform_device_unregister(&sa1100_pcmcia_device);


#ifdef CONFIG_CPU_FREQ
	cpufreq_unregister_notifier(&sa1100_pcmcia_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
#endif

	driver_unregister(&sa1100_pcmcia_driver);
}

MODULE_AUTHOR("John Dorsey <john+@cs.cmu.edu>");
MODULE_DESCRIPTION("Linux PCMCIA Card Services: SA-1100 Socket Controller");
MODULE_LICENSE("Dual MPL/GPL");

module_init(sa1100_pcmcia_init);
module_exit(sa1100_pcmcia_exit);

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
#include <linux/interrupt.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "sa11xx_core.h"
#include "sa1100.h"

#ifdef PCMCIA_DEBUG
static int pc_debug;
#endif

/* This structure maintains housekeeping state for each socket, such
 * as the last known values of the card detect pins, or the Card Services
 * callback value associated with the socket:
 */
static struct sa1100_pcmcia_socket sa1100_pcmcia_socket[SA1100_PCMCIA_MAX_SOCK];

#define PCMCIA_SOCKET(x)	(sa1100_pcmcia_socket + (x))

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
 * that's section 10.2.5 in _my_ version of the manual ;)
 */
static unsigned int
sa1100_pcmcia_default_mecr_timing(struct sa1100_pcmcia_socket *skt,
				  unsigned int cpu_speed,
				  unsigned int cmd_time)
{
	return sa1100_pcmcia_mecr_bs(cmd_time, cpu_speed);
}

static unsigned short
calc_speed(unsigned short *spds, int num, unsigned short dflt)
{
	unsigned short speed = 0;
	int i;

	for (i = 0; i < num; i++)
		if (speed < spds[i])
			speed = spds[i];
	if (speed == 0)
		speed = dflt;

	return speed;
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
	u32 mecr, old_mecr;
	unsigned long flags;
	unsigned short speed;
	unsigned int bs_io, bs_mem, bs_attr;
	int i;

	speed = calc_speed(skt->spd_io, MAX_IO_WIN, SA1100_PCMCIA_IO_ACCESS);
	bs_io = skt->ops->socket_get_timing(skt, cpu_clock, speed);

	speed = calc_speed(skt->spd_mem, MAX_WIN, SA1100_PCMCIA_3V_MEM_ACCESS);
	bs_mem = skt->ops->socket_get_timing(skt, cpu_clock, speed);

	speed = calc_speed(skt->spd_attr, MAX_WIN, SA1100_PCMCIA_3V_MEM_ACCESS);
	bs_attr = skt->ops->socket_get_timing(skt, cpu_clock, speed);

	local_irq_save(flags);

	old_mecr = mecr = MECR;
	MECR_FAST_SET(mecr, skt->nr, 0);
	MECR_BSIO_SET(mecr, skt->nr, bs_io);
	MECR_BSA_SET(mecr, skt->nr, bs_attr);
	MECR_BSM_SET(mecr, skt->nr, bs_mem);
	if (old_mecr != mecr)
		MECR = mecr;

	local_irq_restore(flags);

	DEBUG(4, "%s(): sock %u FAST %X  BSM %X  BSA %X  BSIO %X\n",
	      __FUNCTION__, skt->nr, MECR_FAST_GET(mecr, skt->nr),
	      MECR_BSM_GET(mecr, skt->nr), MECR_BSA_GET(mecr, skt->nr),
	      MECR_BSIO_GET(mecr, skt->nr));

	return 0;
}

static unsigned int sa1100_pcmcia_skt_state(struct sa1100_pcmcia_socket *skt)
{
	struct pcmcia_state state;
	unsigned int stat;

	memset(&state, 0, sizeof(struct pcmcia_state));

	skt->ops->socket_state(skt, &state);

	stat = state.detect  ? SS_DETECT : 0;
	stat |= state.ready  ? SS_READY  : 0;
	stat |= state.wrprot ? SS_WRPROT : 0;
	stat |= state.vs_3v  ? SS_3VCARD : 0;
	stat |= state.vs_Xv  ? SS_XVCARD : 0;

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
	return stat;
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
	int ret;

	ret = skt->ops->configure_socket(skt, state);
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

	DEBUG(2, "%s(): initializing socket %u\n", __FUNCTION__, skt->nr);

	skt->ops->socket_init(skt);
	sa1100_pcmcia_config_skt(skt, &dead_socket);

	return 0;
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
		skt->ops->socket_suspend(skt);

	return ret;
}


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
	struct sa1100_pcmcia_socket *skt = data;
	unsigned int events;

	DEBUG(4, "%s(): entering PCMCIA monitoring thread\n", __FUNCTION__);

	do {
		unsigned int status;

		status = sa1100_pcmcia_skt_state(skt);

		events = (status ^ skt->status) & skt->cs_state.csc_mask;
		skt->status = status;

		DEBUG(2, "events: %s%s%s%s%s%s\n",
			events == 0         ? "<NONE>"   : "",
			events & SS_DETECT  ? "DETECT "  : "",
			events & SS_READY   ? "READY "   : "",
			events & SS_BATDEAD ? "BATDEAD " : "",
			events & SS_BATWARN ? "BATWARN " : "",
			events & SS_STSCHG  ? "STSCHG "  : "");

		if (events && skt->handler != NULL)
			skt->handler(skt->handler_info, events);
	} while (events);
}

/* sa1100_pcmcia_poll_event()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Let's poll for events in addition to IRQs since IRQ only is unreliable...
 */
static void sa1100_pcmcia_poll_event(unsigned long dummy)
{
	struct sa1100_pcmcia_socket *skt = (struct sa1100_pcmcia_socket *)dummy;
	DEBUG(4, "%s(): polling for events\n", __FUNCTION__);

	mod_timer(&skt->poll_timer, jiffies + SA1100_PCMCIA_POLL_PERIOD);

	schedule_work(&skt->work);
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
static void sa1100_pcmcia_interrupt(int irq, void *dev, struct pt_regs *regs)
{
	struct sa1100_pcmcia_socket *skt = dev;

	DEBUG(3, "%s(): servicing IRQ %d\n", __FUNCTION__, irq);

	schedule_work(&skt->work);
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

	if (handler) {
		if (!try_module_get(skt->ops->owner))
			return -ENODEV;
		skt->handler_info = info;
		skt->handler = handler;
	} else {
		skt->handler = NULL;
		module_put(skt->ops->owner);
	}

	return 0;
}


/* sa1100_pcmcia_inquire_socket()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * Implements the inquire_socket() operation for the in-kernel PCMCIA
 * service (formerly SS_InquireSocket in Card Services).  We set
 * SS_CAP_STATIC_MAP, which disables the memory resource database
 * check. (Mapped memory is set up within the socket driver itself.)
 *
 * In conjunction with the STATIC_MAP capability is a new field,
 * `io_offset', recommended by David Hinds. Rather than go through
 * the SetIOMap interface (which is not quite suited for communicating
 * window locations up from the socket driver), we just pass up
 * an offset which is applied to client-requested base I/O addresses
 * in alloc_io_space().
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

	if (skt) {
		DEBUG(2, "%s() for sock %u\n", __FUNCTION__, skt->nr);

		cap->features  = SS_CAP_STATIC_MAP|SS_CAP_PCCARD;
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

	skt->status = sa1100_pcmcia_skt_state(skt);
	*status = skt->status;

	return 0;
}


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
	unsigned short speed = map->speed;

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
		if (speed == 0)
			speed = SA1100_PCMCIA_IO_ACCESS;
	} else {
		speed = 0;
	}

	skt->spd_io[map->map] = speed;
	sa1100_pcmcia_set_mecr(skt, cpufreq_get(0));

	if (map->stop == 1)
		map->stop = PAGE_SIZE-1;

	map->stop -= map->start;
	map->stop += (unsigned long)skt->virt_io;
	map->start = (unsigned long)skt->virt_io;

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
	struct resource *res;
	unsigned short speed = map->speed;

	DEBUG(2, "%s() for sock %u\n", __FUNCTION__, skt->nr);

	DEBUG(3, "\tmap %u speed %u card_start %08x\n",
		map->map, map->speed, map->card_start);
	DEBUG(3, "\tflags: %s%s%s%s%s%s%s%s\n",
		(map->flags==0)?"<NONE>":"",
		(map->flags&MAP_ACTIVE)?"ACTIVE ":"",
		(map->flags&MAP_16BIT)?"16BIT ":"",
		(map->flags&MAP_AUTOSZ)?"AUTOSZ ":"",
		(map->flags&MAP_0WS)?"0WS ":"",
		(map->flags&MAP_WRPROT)?"WRPROT ":"",
		(map->flags&MAP_ATTRIB)?"ATTRIB ":"",
		(map->flags&MAP_USE_WAIT)?"USE_WAIT ":"");

	if (map->map >= MAX_WIN)
		return -EINVAL;

	if (map->flags & MAP_ACTIVE) {
		if (speed == 0)
			speed = 300;
	} else {
		speed = 0;
	}

	if (map->flags & MAP_ATTRIB) {
		res = &skt->res_attr;
		skt->spd_attr[map->map] = speed;
		skt->spd_mem[map->map] = 0;
	} else {
		res = &skt->res_mem;
		skt->spd_attr[map->map] = 0;
		skt->spd_mem[map->map] = speed;
	}

	sa1100_pcmcia_set_mecr(skt, cpufreq_get(0));

	map->sys_stop -= map->sys_start;
	map->sys_stop += res->start + map->card_start;
	map->sys_start = res->start + map->card_start;

	return 0;
}


#if defined(CONFIG_PROC_FS)

struct bittbl {
	unsigned int mask;
	const char *name;
};

static struct bittbl status_bits[] = {
	{ SS_WRPROT,		"SS_WRPROT"	},
	{ SS_BATDEAD,		"SS_BATDEAD"	},
	{ SS_BATWARN,		"SS_BATWARN"	},
	{ SS_READY,		"SS_READY"	},
	{ SS_DETECT,		"SS_DETECT"	},
	{ SS_POWERON,		"SS_POWERON"	},
	{ SS_STSCHG,		"SS_STSCHG"	},
	{ SS_3VCARD,		"SS_3VCARD"	},
	{ SS_XVCARD,		"SS_XVCARD"	},
};

static struct bittbl conf_bits[] = {
	{ SS_PWR_AUTO,		"SS_PWR_AUTO"	},
	{ SS_IOCARD,		"SS_IOCARD"	},
	{ SS_RESET,		"SS_RESET"	},
	{ SS_DMA_MODE,		"SS_DMA_MODE"	},
	{ SS_SPKR_ENA,		"SS_SPKR_ENA"	},
	{ SS_OUTPUT_ENA,	"SS_OUTPUT_ENA"	},
	{ SS_DEBOUNCED,		"SS_DEBOUNCED"	},
};

static void
dump_bits(char **p, const char *prefix, unsigned int val, struct bittbl *bits, int sz)
{
	char *b = *p;
	int i;

	b += sprintf(b, "%-9s:", prefix);
	for (i = 0; i < sz; i++)
		if (val & bits[i].mask)
			b += sprintf(b, " %s", bits[i].name);
	*b++ = '\n';
	*p = b;
}

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

	p+=sprintf(p, "slot     : %d\n", skt->nr);

	dump_bits(&p, "status", skt->status,
		  status_bits, ARRAY_SIZE(status_bits));
	dump_bits(&p, "csc_mask", skt->cs_state.csc_mask,
		  status_bits, ARRAY_SIZE(status_bits));
	dump_bits(&p, "cs_flags", skt->cs_state.flags,
		  conf_bits, ARRAY_SIZE(conf_bits));

	p+=sprintf(p, "Vcc      : %d\n", skt->cs_state.Vcc);
	p+=sprintf(p, "Vpp      : %d\n", skt->cs_state.Vpp);
	p+=sprintf(p, "IRQ      : %d (%d)\n", skt->cs_state.io_irq, skt->irq);

	p+=sprintf(p, "I/O      : %u (%u)\n",
		calc_speed(skt->spd_io, MAX_IO_WIN, SA1100_PCMCIA_IO_ACCESS),
		sa1100_pcmcia_cmd_time(clock, MECR_BSIO_GET(mecr, skt->nr)));

	p+=sprintf(p, "attribute: %u (%u)\n",
		calc_speed(skt->spd_attr, MAX_WIN, SA1100_PCMCIA_3V_MEM_ACCESS),
		sa1100_pcmcia_cmd_time(clock, MECR_BSA_GET(mecr, skt->nr)));

	p+=sprintf(p, "common   : %u (%u)\n",
		calc_speed(skt->spd_mem, MAX_WIN, SA1100_PCMCIA_3V_MEM_ACCESS),
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

	if ((entry = create_proc_entry("status", 0, base)) == NULL){
		printk(KERN_ERR "unable to install \"status\" procfs entry\n");
		return;
	}
	entry->read_proc = sa1100_pcmcia_proc_status;
	entry->data = PCMCIA_SOCKET(sock);
}
#else
#define sa1100_pcmcia_proc_setup	NULL
#endif  /* defined(CONFIG_PROC_FS) */

static struct pccard_operations sa11xx_pcmcia_operations = {
	.owner			= THIS_MODULE,
	.init			= sa1100_pcmcia_sock_init,
	.suspend		= sa1100_pcmcia_suspend,
	.register_callback	= sa1100_pcmcia_register_callback,
	.inquire_socket		= sa1100_pcmcia_inquire_socket,
	.get_status		= sa1100_pcmcia_get_status,
	.get_socket		= sa1100_pcmcia_get_socket,
	.set_socket		= sa1100_pcmcia_set_socket,
	.set_io_map		= sa1100_pcmcia_set_io_map,
	.set_mem_map		= sa1100_pcmcia_set_mem_map,
	.proc_setup		= sa1100_pcmcia_proc_setup
};

int sa11xx_request_irqs(struct sa1100_pcmcia_socket *skt, struct pcmcia_irqs *irqs, int nr)
{
	int i, res = 0;

	for (i = 0; i < nr; i++) {
		if (irqs[i].sock != skt->nr)
			continue;
		res = request_irq(irqs[i].irq, sa1100_pcmcia_interrupt,
				  SA_INTERRUPT, irqs[i].str, skt);
		if (res)
			break;
		set_irq_type(irqs[i].irq, IRQT_NOEDGE);
	}

	if (res) {
		printk(KERN_ERR "PCMCIA: request for IRQ%d failed (%d)\n",
			irqs[i].irq, res);

		while (i--)
			if (irqs[i].sock == skt->nr)
				free_irq(irqs[i].irq, skt);
	}
	return res;
}
EXPORT_SYMBOL(sa11xx_request_irqs);

void sa11xx_free_irqs(struct sa1100_pcmcia_socket *skt, struct pcmcia_irqs *irqs, int nr)
{
	int i;

	for (i = 0; i < nr; i++)
		if (irqs[i].sock == skt->nr)
			free_irq(irqs[i].irq, skt);
}
EXPORT_SYMBOL(sa11xx_free_irqs);

void sa11xx_disable_irqs(struct sa1100_pcmcia_socket *skt, struct pcmcia_irqs *irqs, int nr)
{
	int i;

	for (i = 0; i < nr; i++)
		if (irqs[i].sock == skt->nr)
			set_irq_type(irqs[i].irq, IRQT_NOEDGE);
}
EXPORT_SYMBOL(sa11xx_disable_irqs);

void sa11xx_enable_irqs(struct sa1100_pcmcia_socket *skt, struct pcmcia_irqs *irqs, int nr)
{
	int i;

	for (i = 0; i < nr; i++)
		if (irqs[i].sock == skt->nr) {
			set_irq_type(irqs[i].irq, IRQT_RISING);
			set_irq_type(irqs[i].irq, IRQT_BOTHEDGE);
		}
}
EXPORT_SYMBOL(sa11xx_enable_irqs);

static const char *skt_names[] = {
	"PCMCIA socket 0",
	"PCMCIA socket 1",
};

int sa11xx_drv_pcmcia_probe(struct device *dev, struct pcmcia_low_level *ops, int first, int nr)
{
	struct pcmcia_socket_class_data *cls;
	unsigned int cpu_clock;
	int ret, i;

	cls = kmalloc(sizeof(struct pcmcia_socket_class_data), GFP_KERNEL);
	if (!cls) {
		ret = -ENOMEM;
		goto out;
	}

	memset(cls, 0, sizeof(struct pcmcia_socket_class_data));
	cls->ops	= &sa11xx_pcmcia_operations;
	cls->nsock	= nr;

	/*
	 * set default MECR calculation if the board specific
	 * code did not specify one...
	 */
	if (!ops->socket_get_timing)
		ops->socket_get_timing = sa1100_pcmcia_default_mecr_timing;

	cpu_clock = cpufreq_get(0);

	/*
	 * Initialise the per-socket structure.
	 */
	for (i = 0; i < nr; i++) {
		struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(i);
		memset(skt, 0, sizeof(*skt));

		INIT_WORK(&skt->work, sa1100_pcmcia_task_handler, skt);

		init_timer(&skt->poll_timer);
		skt->poll_timer.function = sa1100_pcmcia_poll_event;
		skt->poll_timer.data = (unsigned long)skt;
		skt->poll_timer.expires = jiffies + SA1100_PCMCIA_POLL_PERIOD;

		skt->nr		= first + i;
		skt->irq	= NO_IRQ;
		skt->dev	= dev;
		skt->ops	= ops;

		skt->res_skt.start	= _PCMCIA(skt->nr);
		skt->res_skt.end	= _PCMCIA(skt->nr) + PCMCIASp - 1;
		skt->res_skt.name	= skt_names[skt->nr];
		skt->res_skt.flags	= IORESOURCE_MEM;

		ret = request_resource(&iomem_resource, &skt->res_skt);
		if (ret)
			goto out_err_1;

		skt->res_io.start	= _PCMCIAIO(skt->nr);
		skt->res_io.end		= _PCMCIAIO(skt->nr) + PCMCIAIOSp - 1;
		skt->res_io.name	= "io";
		skt->res_io.flags	= IORESOURCE_MEM | IORESOURCE_BUSY;

		ret = request_resource(&skt->res_skt, &skt->res_io);
		if (ret)
			goto out_err_2;

		skt->res_mem.start	= _PCMCIAMem(skt->nr);
		skt->res_mem.end	= _PCMCIAMem(skt->nr) + PCMCIAMemSp - 1;
		skt->res_mem.name	= "memory";
		skt->res_mem.flags	= IORESOURCE_MEM;

		ret = request_resource(&skt->res_skt, &skt->res_mem);
		if (ret)
			goto out_err_3;

		skt->res_attr.start	= _PCMCIAAttr(skt->nr);
		skt->res_attr.end	= _PCMCIAAttr(skt->nr) + PCMCIAAttrSp - 1;
		skt->res_attr.name	= "attribute";
		skt->res_attr.flags	= IORESOURCE_MEM;
		
		ret = request_resource(&skt->res_skt, &skt->res_attr);
		if (ret)
			goto out_err_4;

		skt->virt_io = ioremap(skt->res_io.start, 0x10000);
		if (skt->virt_io == NULL) {
			ret = -ENOMEM;
			goto out_err_5;
		}

		/*
		 * We initialize the MECR to default values here, because
		 * we are not guaranteed to see a SetIOMap operation at
		 * runtime.
		 */
		sa1100_pcmcia_set_mecr(skt, cpu_clock);

		ret = ops->hw_init(skt);
		if (ret)
			goto out_err_6;

		skt->status = sa1100_pcmcia_skt_state(skt);
		add_timer(&skt->poll_timer);
	}

	dev->class_data = cls;
	return 0;

	do {
		struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(i);

		del_timer_sync(&skt->poll_timer);
		flush_scheduled_work();

		ops->hw_shutdown(skt);
 out_err_6:
		iounmap(skt->virt_io);
 out_err_5:
		release_resource(&skt->res_attr);
 out_err_4:
		release_resource(&skt->res_mem);
 out_err_3:
		release_resource(&skt->res_io);
 out_err_2:
		release_resource(&skt->res_skt);
 out_err_1:
		i--;
	} while (i > 0);

	kfree(cls);

 out:
	return ret;
}
EXPORT_SYMBOL(sa11xx_drv_pcmcia_probe);

int sa11xx_drv_pcmcia_remove(struct device *dev)
{
	struct pcmcia_socket_class_data *cls = dev->class_data;
	int i;

	dev->class_data = NULL;

	for (i = 0; i < cls->nsock; i++) {
		struct sa1100_pcmcia_socket *skt = PCMCIA_SOCKET(cls->sock_offset + i);

		skt->ops->hw_shutdown(skt);

		del_timer_sync(&skt->poll_timer);
		flush_scheduled_work();

		sa1100_pcmcia_config_skt(skt, &dead_socket);

		iounmap(skt->virt_io);
		skt->virt_io = NULL;
		release_resource(&skt->res_attr);
		release_resource(&skt->res_mem);
		release_resource(&skt->res_io);
		release_resource(&skt->res_skt);
	}

	kfree(cls);

	return 0;
}
EXPORT_SYMBOL(sa11xx_drv_pcmcia_remove);

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

static int __init sa11xx_pcmcia_init(void)
{
	int ret;

	printk(KERN_INFO "SA11xx PCMCIA (CS release %s)\n", CS_RELEASE);

	ret = cpufreq_register_notifier(&sa1100_pcmcia_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
	if (ret < 0)
		printk(KERN_ERR "Unable to register CPU frequency change "
			"notifier (%d)\n", ret);

	return ret;
}
module_init(sa11xx_pcmcia_init);

static void __exit sa11xx_pcmcia_exit(void)
{
	cpufreq_unregister_notifier(&sa1100_pcmcia_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
}

module_exit(sa11xx_pcmcia_exit);
#endif

MODULE_AUTHOR("John Dorsey <john+@cs.cmu.edu>");
MODULE_DESCRIPTION("Linux PCMCIA Card Services: SA-11xx core socket driver");
MODULE_LICENSE("Dual MPL/GPL");

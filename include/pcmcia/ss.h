/*
 * ss.h 1.28 2000/06/12 21:55:40
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#ifndef _LINUX_SS_H
#define _LINUX_SS_H

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <linux/device.h>

/* Definitions for card status flags for GetStatus */
#define SS_WRPROT	0x0001
#define SS_CARDLOCK	0x0002
#define SS_EJECTION	0x0004
#define SS_INSERTION	0x0008
#define SS_BATDEAD	0x0010
#define SS_BATWARN	0x0020
#define SS_READY	0x0040
#define SS_DETECT	0x0080
#define SS_POWERON	0x0100
#define SS_GPI		0x0200
#define SS_STSCHG	0x0400
#define SS_CARDBUS	0x0800
#define SS_3VCARD	0x1000
#define SS_XVCARD	0x2000
#define SS_PENDING	0x4000
#define SS_ZVCARD	0x8000

/* InquireSocket capabilities */
#define SS_CAP_PAGE_REGS	0x0001
#define SS_CAP_VIRTUAL_BUS	0x0002
#define SS_CAP_MEM_ALIGN	0x0004
#define SS_CAP_STATIC_MAP	0x0008
#define SS_CAP_PCCARD		0x4000
#define SS_CAP_CARDBUS		0x8000

/* for GetSocket, SetSocket */
typedef struct socket_state_t {
    u_int	flags;
    u_int	csc_mask;
    u_char	Vcc, Vpp;
    u_char	io_irq;
} socket_state_t;

extern socket_state_t dead_socket;

/* Socket configuration flags */
#define SS_PWR_AUTO	0x0010
#define SS_IOCARD	0x0020
#define SS_RESET	0x0040
#define SS_DMA_MODE	0x0080
#define SS_SPKR_ENA	0x0100
#define SS_OUTPUT_ENA	0x0200

/* Flags for I/O port and memory windows */
#define MAP_ACTIVE	0x01
#define MAP_16BIT	0x02
#define MAP_AUTOSZ	0x04
#define MAP_0WS		0x08
#define MAP_WRPROT	0x10
#define MAP_ATTRIB	0x20
#define MAP_USE_WAIT	0x40
#define MAP_PREFETCH	0x80

/* Use this just for bridge windows */
#define MAP_IOSPACE	0x20

typedef struct pccard_io_map {
    u_char	map;
    u_char	flags;
    u_short	speed;
    ioaddr_t	start, stop;
} pccard_io_map;

typedef struct pccard_mem_map {
    u_char	map;
    u_char	flags;
    u_short	speed;
    u_long	sys_start, sys_stop;
    u_int	card_start;
} pccard_mem_map;

typedef struct cb_bridge_map {
    u_char	map;
    u_char	flags;
    u_int	start, stop;
} cb_bridge_map;

/*
 * Socket operations.
 */
struct pcmcia_socket;

struct pccard_operations {
	int (*init)(struct pcmcia_socket *sock);
	int (*suspend)(struct pcmcia_socket *sock);
	int (*register_callback)(struct pcmcia_socket *sock, void (*handler)(void *, unsigned int), void * info);
	int (*get_status)(struct pcmcia_socket *sock, u_int *value);
	int (*get_socket)(struct pcmcia_socket *sock, socket_state_t *state);
	int (*set_socket)(struct pcmcia_socket *sock, socket_state_t *state);
	int (*set_io_map)(struct pcmcia_socket *sock, struct pccard_io_map *io);
	int (*set_mem_map)(struct pcmcia_socket *sock, struct pccard_mem_map *mem);
};

/*
 *  Calls to set up low-level "Socket Services" drivers
 */
struct pcmcia_socket;

typedef struct erase_busy_t {
	eraseq_entry_t		*erase;
	client_handle_t		client;
	struct timer_list	timeout;
	struct erase_busy_t	*prev, *next;
} erase_busy_t;

typedef struct io_window_t {
	u_int			Attributes;
	ioaddr_t		BasePort, NumPorts;
	ioaddr_t		InUse, Config;
} io_window_t;

#define WINDOW_MAGIC	0xB35C
typedef struct window_t {
	u_short			magic;
	u_short			index;
	client_handle_t		handle;
	struct pcmcia_socket 	*sock;
	u_long			base;
	u_long			size;
	pccard_mem_map		ctl;
} window_t;

/* Maximum number of IO windows per socket */
#define MAX_IO_WIN 2

/* Maximum number of memory windows per socket */
#define MAX_WIN 4

struct config_t;
struct region_t;

struct pcmcia_socket {
	struct module			*owner;
	spinlock_t			lock;
	socket_state_t			socket;
	u_int				state;
	u_short				functions;
	u_short				lock_count;
	client_handle_t			clients;
	pccard_mem_map			cis_mem;
	u_char				*cis_virt;
	struct config_t			*config;
	struct {
		u_int			AssignedIRQ;
		u_int			Config;
	} irq;
	io_window_t			io[MAX_IO_WIN];
	window_t			win[MAX_WIN];
	struct region_t			*c_region, *a_region;
	erase_busy_t			erase_busy;
	struct list_head		cis_cache;
	u_int				fake_cis_len;
	char				*fake_cis;

	struct list_head		socket_list;
	struct completion		socket_released;

 	/* deprecated */
	unsigned int			sock;		/* socket number */


	/* socket capabilities */
	u_int				features;
	u_int				irq_mask;
	u_int				map_size;
	ioaddr_t			io_offset;
	u_char				pci_irq;
	struct pci_dev *		cb_dev;

	/* socket operations */
	struct pccard_operations *	ops;

	/* Zoom video behaviour is so chip specific its not worth adding
	   this to _ops */
	void 				(*zoom_video)(struct pcmcia_socket *, int);
                           
	/* state thread */
	struct semaphore		skt_sem;	/* protects socket h/w state */

	struct task_struct		*thread;
	struct completion		thread_done;
	wait_queue_head_t		thread_wait;
	spinlock_t			thread_lock;	/* protects thread_events */
	unsigned int			thread_events;

	/* pcmcia (16-bit) */
	struct pcmcia_bus_socket	*pcmcia;

	/* cardbus (32-bit) */
#ifdef CONFIG_CARDBUS
	struct resource *		cb_cis_res;
	u_char				*cb_cis_virt;
#endif

	/* socket device */
	struct class_device		dev;
	void				*driver_data;	/* data internal to the socket driver */

};

struct pcmcia_socket * pcmcia_get_socket_by_nr(unsigned int nr);



extern void pcmcia_parse_events(struct pcmcia_socket *socket, unsigned int events);
extern int pcmcia_register_socket(struct pcmcia_socket *socket);
extern void pcmcia_unregister_socket(struct pcmcia_socket *socket);

extern struct class pcmcia_socket_class;

/* socket drivers are expected to use these callbacks in their .drv struct */
extern int pcmcia_socket_dev_suspend(struct device *dev, u32 state);
extern int pcmcia_socket_dev_resume(struct device *dev);

#endif /* _LINUX_SS_H */

/*
 * cs_internal.h 1.57 2002/10/24 06:11:43
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
 *  are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 */

#ifndef _LINUX_CS_INTERNAL_H
#define _LINUX_CS_INTERNAL_H

#include <linux/config.h>

#define ERASEQ_MAGIC	0xFA67
typedef struct eraseq_t {
    u_short		eraseq_magic;
    client_handle_t	handle;
    int			count;
    eraseq_entry_t	*entry;
} eraseq_t;

#define CLIENT_MAGIC 	0x51E6
typedef struct client_t {
    u_short		client_magic;
    struct pcmcia_socket *Socket;
    u_char		Function;
    dev_info_t		dev_info;
    u_int		Attributes;
    u_int		state;
    event_t		EventMask, PendingEvents;
    int (*event_handler)(event_t event, int priority,
			 event_callback_args_t *);
    event_callback_args_t event_callback_args;
    struct client_t 	*next;
    u_int		mtd_count;
    wait_queue_head_t	mtd_req;
    erase_busy_t	erase_busy;
} client_t;

/* Flags in client state */
#define CLIENT_CONFIG_LOCKED	0x0001
#define CLIENT_IRQ_REQ		0x0002
#define CLIENT_IO_REQ		0x0004
#define CLIENT_UNBOUND		0x0008
#define CLIENT_STALE		0x0010
#define CLIENT_WIN_REQ(i)	(0x20<<(i))
#define CLIENT_CARDBUS		0x8000

#define REGION_MAGIC	0xE3C9
typedef struct region_t {
    u_short		region_magic;
    u_short		state;
    dev_info_t		dev_info;
    client_handle_t	mtd;
    u_int		MediaID;
    region_info_t	info;
} region_t;

#define REGION_STALE	0x01

/* Each card function gets one of these guys */
typedef struct config_t {
    u_int		state;
    u_int		Attributes;
    u_int		Vcc, Vpp1, Vpp2;
    u_int		IntType;
    u_int		ConfigBase;
    u_char		Status, Pin, Copy, Option, ExtStatus;
    u_int		Present;
    u_int		CardValues;
    io_req_t		io;
    struct {
	u_int		Attributes;
    } irq;
} config_t;

struct cis_cache_entry {
	struct list_head	node;
	unsigned int		addr;
	unsigned int		len;
	unsigned int		attr;
	unsigned char		cache[0];
};

/* Flags in config state */
#define CONFIG_LOCKED		0x01
#define CONFIG_IRQ_REQ		0x02
#define CONFIG_IO_REQ		0x04

/* Flags in socket state */
#define SOCKET_PRESENT		0x0008
#define SOCKET_INUSE		0x0010
#define SOCKET_SUSPEND		0x0080
#define SOCKET_WIN_REQ(i)	(0x0100<<(i))
#define SOCKET_REGION_INFO	0x4000
#define SOCKET_CARDBUS		0x8000
#define SOCKET_CARDBUS_CONFIG	0x10000

static inline int cs_socket_get(struct pcmcia_socket *skt)
{
	int ret;

	WARN_ON(skt->state & SOCKET_INUSE);

	ret = try_module_get(skt->owner);
	if (ret)
		skt->state |= SOCKET_INUSE;
	return ret;
}

static inline void cs_socket_put(struct pcmcia_socket *skt)
{
	if (skt->state & SOCKET_INUSE) {
		skt->state &= ~SOCKET_INUSE;
		module_put(skt->owner);
	}
}

#define CHECK_HANDLE(h) \
    (((h) == NULL) || ((h)->client_magic != CLIENT_MAGIC))

#define CHECK_SOCKET(s) \
    (((s) >= sockets) || (socket_table[s]->ops == NULL))

#define SOCKET(h) (h->Socket)
#define CONFIG(h) (&SOCKET(h)->config[(h)->Function])

#define CHECK_REGION(r) \
    (((r) == NULL) || ((r)->region_magic != REGION_MAGIC))

#define CHECK_ERASEQ(q) \
    (((q) == NULL) || ((q)->eraseq_magic != ERASEQ_MAGIC))

#define EVENT(h, e, p) \
    ((h)->event_handler((e), (p), &(h)->event_callback_args))

/* In cardbus.c */
int cb_alloc(struct pcmcia_socket *s);
void cb_free(struct pcmcia_socket *s);
int read_cb_mem(struct pcmcia_socket *s, int space, u_int addr, u_int len, void *ptr);

/* In cistpl.c */
int read_cis_mem(struct pcmcia_socket *s, int attr,
		 u_int addr, u_int len, void *ptr);
void write_cis_mem(struct pcmcia_socket *s, int attr,
		   u_int addr, u_int len, void *ptr);
void release_cis_mem(struct pcmcia_socket *s);
void destroy_cis_cache(struct pcmcia_socket *s);
int verify_cis_cache(struct pcmcia_socket *s);
void preload_cis_cache(struct pcmcia_socket *s);
int get_first_tuple(client_handle_t handle, tuple_t *tuple);
int get_next_tuple(client_handle_t handle, tuple_t *tuple);
int get_tuple_data(client_handle_t handle, tuple_t *tuple);
int parse_tuple(client_handle_t handle, tuple_t *tuple, cisparse_t *parse);
int validate_cis(client_handle_t handle, cisinfo_t *info);
int replace_cis(client_handle_t handle, cisdump_t *cis);
int read_tuple(client_handle_t handle, cisdata_t code, void *parse);

/* In bulkmem.c */
int get_first_region(client_handle_t handle, region_info_t *rgn);
int get_next_region(client_handle_t handle, region_info_t *rgn);
int register_mtd(client_handle_t handle, mtd_reg_t *reg);
int register_erase_queue(client_handle_t *handle, eraseq_hdr_t *header);
int deregister_erase_queue(eraseq_handle_t eraseq);
int check_erase_queue(eraseq_handle_t eraseq);
int open_memory(client_handle_t *handle, open_mem_t *open);
int close_memory(memory_handle_t handle);
int read_memory(memory_handle_t handle, mem_op_t *req, caddr_t buf);
int write_memory(memory_handle_t handle, mem_op_t *req, caddr_t buf);
int copy_memory(memory_handle_t handle, copy_op_t *req);

/* In rsrc_mgr */
void validate_mem(struct pcmcia_socket *s);
struct resource *find_io_region(unsigned long base, int num, unsigned long align,
		   char *name, struct pcmcia_socket *s);
int adjust_io_region(struct resource *res, unsigned long r_start,
		     unsigned long r_end, struct pcmcia_socket *s);
int find_mem_region(u_long *base, u_long num, u_long align,
		    int low, char *name, struct pcmcia_socket *s);
int try_irq(u_int Attributes, int irq, int specific);
void undo_irq(u_int Attributes, int irq);
int adjust_resource_info(client_handle_t handle, adjust_t *adj);
void release_resource_db(void);

extern struct rw_semaphore pcmcia_socket_list_rwsem;
extern struct list_head pcmcia_socket_list;

#define cs_socket_name(skt)	((skt)->dev.class_id)

#ifdef DEBUG
extern int cs_debug_level(int);

#define cs_dbg(skt, lvl, fmt, arg...) do {		\
	if (cs_debug_level(lvl))			\
		printk(KERN_DEBUG "cs: %s: " fmt, 	\
		       cs_socket_name(skt) , ## arg);	\
} while (0)

#else
#define cs_dbg(skt, lvl, fmt, arg...) do { } while (0)
#endif

#define cs_err(skt, fmt, arg...) \
	printk(KERN_ERR "cs: %s: " fmt, (skt)->dev.class_id , ## arg)

#endif /* _LINUX_CS_INTERNAL_H */

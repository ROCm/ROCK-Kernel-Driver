/*
 * IEEE 1394 for Linux
 *
 * Copyright (C) 1999 Andreas E. Bombe
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/config.h>
#include <linux/slab.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"


LIST_HEAD(hl_drivers);
rwlock_t hl_drivers_lock = RW_LOCK_UNLOCKED;

LIST_HEAD(addr_space);
rwlock_t addr_space_lock = RW_LOCK_UNLOCKED;

/* addr_space list will have zero and max already included as bounds */
static struct hpsb_address_ops dummy_ops = { NULL, NULL, NULL, NULL };
static struct hpsb_address_serve dummy_zero_addr, dummy_max_addr;

struct hpsb_highlevel *hpsb_register_highlevel(const char *name,
                                               struct hpsb_highlevel_ops *ops)
{
        struct hpsb_highlevel *hl;

        hl = (struct hpsb_highlevel *)kmalloc(sizeof(struct hpsb_highlevel),
                                              GFP_KERNEL);
        if (hl == NULL) {
                return NULL;
        }

        INIT_LIST_HEAD(&hl->hl_list);
        INIT_LIST_HEAD(&hl->addr_list);
        hl->name = name;
        hl->op = ops;

        write_lock_irq(&hl_drivers_lock);
        hl_all_hosts(hl, 1);
        list_add_tail(&hl->hl_list, &hl_drivers);
        write_unlock_irq(&hl_drivers_lock);

        return hl;
}

void hpsb_unregister_highlevel(struct hpsb_highlevel *hl)
{
        struct list_head *entry;
        struct hpsb_address_serve *as;

        if (hl == NULL) {
                return;
        }

        write_lock_irq(&addr_space_lock);
        entry = hl->addr_list.next;

        while (entry != &hl->addr_list) {
                as = list_entry(entry, struct hpsb_address_serve, addr_list);
                list_del(&as->as_list);
                entry = entry->next;
                kfree(as);
        }
        write_unlock_irq(&addr_space_lock);

        write_lock_irq(&hl_drivers_lock);
        list_del(&hl->hl_list);
        hl_all_hosts(hl, 0);
        write_unlock_irq(&hl_drivers_lock);

        kfree(hl);
}

int hpsb_register_addrspace(struct hpsb_highlevel *hl,
                            struct hpsb_address_ops *ops, u64 start, u64 end)
{
        struct hpsb_address_serve *as;
        struct list_head *entry;
        int retval = 0;

        if (((start|end) & 3) || (start >= end) || (end > 0x1000000000000ULL)) {
                HPSB_ERR(__FUNCTION__ " called with invalid addresses");
                return 0;
        }

        as = (struct hpsb_address_serve *)
                kmalloc(sizeof(struct hpsb_address_serve), GFP_KERNEL);
        if (as == NULL) {
                return 0;
        }

        INIT_LIST_HEAD(&as->as_list);
        INIT_LIST_HEAD(&as->addr_list);
        as->op = ops;
        as->start = start;
        as->end = end;

        write_lock_irq(&addr_space_lock);
        entry = addr_space.next;

        while (list_entry(entry, struct hpsb_address_serve, as_list)->end
               <= start) {
                if (list_entry(entry->next, struct hpsb_address_serve, as_list)
                    ->start >= end) {
                        list_add(&as->as_list, entry);
                        list_add_tail(&as->addr_list, &hl->addr_list);
                        retval = 1;
                        break;
                }
                entry = entry->next;
        }
        write_unlock_irq(&addr_space_lock);

        if (retval == 0) {
                kfree(as);
        }

        return retval;
}


void hpsb_listen_channel(struct hpsb_highlevel *hl, struct hpsb_host *host,
                         unsigned int channel)
{
        if (channel > 63) {
                HPSB_ERR(__FUNCTION__ " called with invalid channel");
                return;
        }

        if (host->iso_listen_count[channel]++ == 0) {
                host->template->devctl(host, ISO_LISTEN_CHANNEL, channel);
        }
}

void hpsb_unlisten_channel(struct hpsb_highlevel *hl, struct hpsb_host *host, 
                           unsigned int channel)
{
        if (channel > 63) {
                HPSB_ERR(__FUNCTION__ " called with invalid channel");
                return;
        }

        if (--host->iso_listen_count[channel] == 0) {
                host->template->devctl(host, ISO_UNLISTEN_CHANNEL, channel);
        }
}


#define DEFINE_MULTIPLEXER(Function)			\
void highlevel_##Function(struct hpsb_host *host)	\
{							\
	struct list_head *lh;				\
	void (*funcptr)(struct hpsb_host*);		\
	read_lock(&hl_drivers_lock);			\
	list_for_each(lh, &hl_drivers) {		\
		funcptr = list_entry(lh, struct hpsb_highlevel, hl_list) \
				->op->Function;		\
		if (funcptr) funcptr(host);		\
	}						\
	read_unlock(&hl_drivers_lock);			\
}

DEFINE_MULTIPLEXER(add_host)
DEFINE_MULTIPLEXER(remove_host)
DEFINE_MULTIPLEXER(host_reset)
#undef DEFINE_MULTIPLEXER

/* Add one host to our list */
void highlevel_add_one_host (struct hpsb_host *host)
{
	if (host->template->initialize_host)
		if (!host->template->initialize_host(host))
			goto fail;
	host->initialized = 1;
	highlevel_add_host (host);
	hpsb_reset_bus (host, LONG_RESET);
fail:
	host->template->number_of_hosts++;
}

void highlevel_iso_receive(struct hpsb_host *host, quadlet_t *data,
                           unsigned int length)
{
        struct list_head *entry;
        struct hpsb_highlevel *hl;
        int channel = (data[0] >> 8) & 0x3f;

        read_lock(&hl_drivers_lock);
        entry = hl_drivers.next;

        while (entry != &hl_drivers) {
                hl = list_entry(entry, struct hpsb_highlevel, hl_list);
                if (hl->op->iso_receive) {
                        hl->op->iso_receive(host, channel, data, length);
                }
                entry = entry->next;
        }
        read_unlock(&hl_drivers_lock);
}

void highlevel_fcp_request(struct hpsb_host *host, int nodeid, int direction,
                           u8 *data, unsigned int length)
{
        struct list_head *entry;
        struct hpsb_highlevel *hl;
        int cts = data[0] >> 4;

        read_lock(&hl_drivers_lock);
        entry = hl_drivers.next;

        while (entry != &hl_drivers) {
                hl = list_entry(entry, struct hpsb_highlevel, hl_list);
                if (hl->op->fcp_request) {
                        hl->op->fcp_request(host, nodeid, direction, cts, data,
                                            length);
                }
                entry = entry->next;
        }
        read_unlock(&hl_drivers_lock);
}

int highlevel_read(struct hpsb_host *host, int nodeid, quadlet_t *buffer,
                   u64 addr, unsigned int length)
{
        struct hpsb_address_serve *as;
        struct list_head *entry;
        unsigned int partlength;
        int rcode = RCODE_ADDRESS_ERROR;

        read_lock(&addr_space_lock);

        entry = addr_space.next;
        as = list_entry(entry, struct hpsb_address_serve, as_list);

        while (as->start <= addr) {
                if (as->end > addr) {
                        partlength = MIN((unsigned int)(as->end - addr),
                                         length);

                        if (as->op->read != NULL) {
                                rcode = as->op->read(host, nodeid, buffer, addr,
                                                     partlength);
                        } else {
                                rcode = RCODE_TYPE_ERROR;
                        }

                        length -= partlength;
                        addr += partlength;

                        if ((rcode != RCODE_COMPLETE) || !length) {
                                break;
                        }
                }

                entry = entry->next;
                as = list_entry(entry, struct hpsb_address_serve, as_list);
        }

        read_unlock(&addr_space_lock);

        if (length && (rcode == RCODE_COMPLETE)) {
                rcode = RCODE_ADDRESS_ERROR;
        }

        return rcode;
}

int highlevel_write(struct hpsb_host *host, int nodeid, int destid,
		    quadlet_t *data, u64 addr, unsigned int length)
{
        struct hpsb_address_serve *as;
        struct list_head *entry;
        unsigned int partlength;
        int rcode = RCODE_ADDRESS_ERROR;

        read_lock(&addr_space_lock);

        entry = addr_space.next;
        as = list_entry(entry, struct hpsb_address_serve, as_list);

        while (as->start <= addr) {
                if (as->end > addr) {
                        partlength = MIN((unsigned int)(as->end - addr),
                                         length);

                        if (as->op->write != NULL) {
                                rcode = as->op->write(host, nodeid, destid, data,
						      addr, partlength);
                        } else {
                                rcode = RCODE_TYPE_ERROR;
                        }

                        length -= partlength;
                        addr += partlength;

                        if ((rcode != RCODE_COMPLETE) || !length) {
                                break;
                        }
                }

                entry = entry->next;
                as = list_entry(entry, struct hpsb_address_serve, as_list);
        }

        read_unlock(&addr_space_lock);

        if (length && (rcode == RCODE_COMPLETE)) {
                rcode = RCODE_ADDRESS_ERROR;
        }

        return rcode;
}


int highlevel_lock(struct hpsb_host *host, int nodeid, quadlet_t *store,
                   u64 addr, quadlet_t data, quadlet_t arg, int ext_tcode)
{
        struct hpsb_address_serve *as;
        struct list_head *entry;
        int rcode = RCODE_ADDRESS_ERROR;

        read_lock(&addr_space_lock);

        entry = addr_space.next;
        as = list_entry(entry, struct hpsb_address_serve, as_list);

        while (as->start <= addr) {
                if (as->end > addr) {
                        if (as->op->lock != NULL) {
                                rcode = as->op->lock(host, nodeid, store, addr,
                                                     data, arg, ext_tcode);
                        } else {
                                rcode = RCODE_TYPE_ERROR;
                        }

                        break;
                }

                entry = entry->next;
                as = list_entry(entry, struct hpsb_address_serve, as_list);
        }

        read_unlock(&addr_space_lock);

        return rcode;
}

int highlevel_lock64(struct hpsb_host *host, int nodeid, octlet_t *store,
                     u64 addr, octlet_t data, octlet_t arg, int ext_tcode)
{
        struct hpsb_address_serve *as;
        struct list_head *entry;
        int rcode = RCODE_ADDRESS_ERROR;

        read_lock(&addr_space_lock);

        entry = addr_space.next;
        as = list_entry(entry, struct hpsb_address_serve, as_list);

        while (as->start <= addr) {
                if (as->end > addr) {
                        if (as->op->lock64 != NULL) {
                                rcode = as->op->lock64(host, nodeid, store,
                                                       addr, data, arg,
                                                       ext_tcode);
                        } else {
                                rcode = RCODE_TYPE_ERROR;
                        }

                        break;
                }

                entry = entry->next;
                as = list_entry(entry, struct hpsb_address_serve, as_list);
        }

        read_unlock(&addr_space_lock);

        return rcode;
}

void init_hpsb_highlevel(void)
{
        INIT_LIST_HEAD(&dummy_zero_addr.as_list);
        INIT_LIST_HEAD(&dummy_zero_addr.addr_list);
        INIT_LIST_HEAD(&dummy_max_addr.as_list);
        INIT_LIST_HEAD(&dummy_max_addr.addr_list);

        dummy_zero_addr.op = dummy_max_addr.op = &dummy_ops;

        dummy_zero_addr.start = dummy_zero_addr.end = 0;
        dummy_max_addr.start = dummy_max_addr.end = ((u64) 1) << 48;

        list_add_tail(&dummy_zero_addr.as_list, &addr_space);
        list_add_tail(&dummy_max_addr.as_list, &addr_space);
}

/*
 * IEEE 1394 for Linux
 *
 * Copyright (C) 1999 Andreas E. Bombe
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 *
 *
 * Contributions:
 *
 * Christian Toegel <christian.toegel@gmx.at>
 *        unregister address space
 *
 * Manfred Weihs <weihs@ict.tuwien.ac.at>
 *        unregister address space
 *
 */

#include <linux/config.h>
#include <linux/slab.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"


LIST_HEAD(hl_drivers);
static DECLARE_MUTEX(hl_drivers_lock);

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

	down(&hl_drivers_lock);
        list_add_tail(&hl->hl_list, &hl_drivers);
	up(&hl_drivers_lock);

	if (hl->op->add_host)
		hl_all_hosts(hl->op->add_host);

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

	down(&hl_drivers_lock);
        list_del(&hl->hl_list);
	up(&hl_drivers_lock);

        if (hl->op->remove_host)
		hl_all_hosts(hl->op->remove_host);

        kfree(hl);
}

int hpsb_register_addrspace(struct hpsb_highlevel *hl,
                            struct hpsb_address_ops *ops, u64 start, u64 end)
{
        struct hpsb_address_serve *as;
        struct list_head *entry;
        int retval = 0;
        unsigned long flags;

        if (((start|end) & 3) || (start >= end) || (end > 0x1000000000000ULL)) {
                HPSB_ERR("%s called with invalid addresses", __FUNCTION__);
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

        write_lock_irqsave(&addr_space_lock, flags);
        entry = addr_space.next;

        while (list_entry(entry, struct hpsb_address_serve, as_list)->end <= start) {
                if (list_entry(entry->next, struct hpsb_address_serve, as_list)->start >= end) {
                        list_add(&as->as_list, entry);
                        list_add_tail(&as->addr_list, &hl->addr_list);
                        retval = 1;
                        break;
                }
                entry = entry->next;
        }
        write_unlock_irqrestore(&addr_space_lock, flags);

        if (retval == 0) {
                kfree(as);
        }

        return retval;
}

int hpsb_unregister_addrspace(struct hpsb_highlevel *hl, u64 start)
{
        int retval = 0;
        struct hpsb_address_serve *as;
        struct list_head *entry;
        unsigned long flags;

        write_lock_irqsave(&addr_space_lock, flags);

        entry = hl->addr_list.next;

        while (entry != &hl->addr_list) {
                as = list_entry(entry, struct hpsb_address_serve, addr_list);
                entry = entry->next;
                if (as->start == start) {
                        list_del(&as->as_list);
                        list_del(&as->addr_list);
                        kfree(as);
                        retval = 1;
                        break;
                }
        }

        write_unlock_irqrestore(&addr_space_lock, flags);

        return retval;
}

int hpsb_listen_channel(struct hpsb_highlevel *hl, struct hpsb_host *host,
                         unsigned int channel)
{
        if (channel > 63) {
                HPSB_ERR("%s called with invalid channel", __FUNCTION__);
                return -EINVAL;
        }

        if (host->iso_listen_count[channel]++ == 0) {
                return host->driver->devctl(host, ISO_LISTEN_CHANNEL, channel);
        }

	return 0;
}

void hpsb_unlisten_channel(struct hpsb_highlevel *hl, struct hpsb_host *host, 
                           unsigned int channel)
{
        if (channel > 63) {
                HPSB_ERR("%s called with invalid channel", __FUNCTION__);
                return;
        }

        if (--host->iso_listen_count[channel] == 0) {
                host->driver->devctl(host, ISO_UNLISTEN_CHANNEL, channel);
        }
}


void highlevel_add_host(struct hpsb_host *host)
{
        struct list_head *entry;
        struct hpsb_highlevel *hl;

	down(&hl_drivers_lock);
        list_for_each(entry, &hl_drivers) {
                hl = list_entry(entry, struct hpsb_highlevel, hl_list);

		if (hl->op->add_host)
			hl->op->add_host(host);
        }
        up(&hl_drivers_lock);
}

void highlevel_remove_host(struct hpsb_host *host)
{
        struct list_head *entry;
        struct hpsb_highlevel *hl;

        down(&hl_drivers_lock);
	list_for_each(entry, &hl_drivers) {
                hl = list_entry(entry, struct hpsb_highlevel, hl_list);

		if (hl->op->remove_host)
			hl->op->remove_host(host);
        }
        up(&hl_drivers_lock);
}

void highlevel_host_reset(struct hpsb_host *host)
{
        struct list_head *entry;
        struct hpsb_highlevel *hl;

	down(&hl_drivers_lock);
	list_for_each(entry, &hl_drivers) {
                hl = list_entry(entry, struct hpsb_highlevel, hl_list);

                if (hl->op->host_reset)
                        hl->op->host_reset(host);
        }
	up(&hl_drivers_lock);
}

void highlevel_iso_receive(struct hpsb_host *host, void *data,
                           unsigned int length)
{
        struct list_head *entry;
        struct hpsb_highlevel *hl;
        int channel = (((quadlet_t *)data)[0] >> 8) & 0x3f;

        down(&hl_drivers_lock);
	list_for_each(entry, &hl_drivers) {
                hl = list_entry(entry, struct hpsb_highlevel, hl_list);

                if (hl->op->iso_receive)
                        hl->op->iso_receive(host, channel, data, length);
        }
        up(&hl_drivers_lock);
}

void highlevel_fcp_request(struct hpsb_host *host, int nodeid, int direction,
                           void *data, unsigned int length)
{
        struct list_head *entry;
        struct hpsb_highlevel *hl;
        int cts = ((quadlet_t *)data)[0] >> 4;

        down(&hl_drivers_lock);
	list_for_each(entry, &hl_drivers) {
                hl = list_entry(entry, struct hpsb_highlevel, hl_list);
                if (hl->op->fcp_request)
                        hl->op->fcp_request(host, nodeid, direction, cts, data, length);
        }
        up(&hl_drivers_lock);
}

int highlevel_read(struct hpsb_host *host, int nodeid, void *data,
                   u64 addr, unsigned int length, u16 flags)
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
                        partlength = min(as->end - addr, (u64) length);

                        if (as->op->read) {
                                rcode = as->op->read(host, nodeid, data,
						     addr, partlength, flags);
                        } else {
                                rcode = RCODE_TYPE_ERROR;
                        }

			(u8 *)data += partlength;
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
		    void *data, u64 addr, unsigned int length, u16 flags)
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
                        partlength = min(as->end - addr, (u64) length);

                        if (as->op->write) {
                                rcode = as->op->write(host, nodeid, destid,
						      data, addr, partlength, flags);
                        } else {
                                rcode = RCODE_TYPE_ERROR;
                        }

			(u8 *)data += partlength;
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
                   u64 addr, quadlet_t data, quadlet_t arg, int ext_tcode, u16 flags)
{
        struct hpsb_address_serve *as;
        struct list_head *entry;
        int rcode = RCODE_ADDRESS_ERROR;

        read_lock(&addr_space_lock);

        entry = addr_space.next;
        as = list_entry(entry, struct hpsb_address_serve, as_list);

        while (as->start <= addr) {
                if (as->end > addr) {
                        if (as->op->lock) {
                                rcode = as->op->lock(host, nodeid, store, addr,
                                                     data, arg, ext_tcode, flags);
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
                     u64 addr, octlet_t data, octlet_t arg, int ext_tcode, u16 flags)
{
        struct hpsb_address_serve *as;
        struct list_head *entry;
        int rcode = RCODE_ADDRESS_ERROR;

        read_lock(&addr_space_lock);

        entry = addr_space.next;
        as = list_entry(entry, struct hpsb_address_serve, as_list);

        while (as->start <= addr) {
                if (as->end > addr) {
                        if (as->op->lock64) {
                                rcode = as->op->lock64(host, nodeid, store,
                                                       addr, data, arg,
                                                       ext_tcode, flags);
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

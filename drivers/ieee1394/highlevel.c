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
#include <linux/list.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "nodemgr.h"


struct hl_host_info {
	struct list_head list;
	struct hpsb_host *host;
	size_t size;
	unsigned long key;
	void *data;
};


static LIST_HEAD(hl_drivers);
static DECLARE_RWSEM(hl_drivers_sem);

static LIST_HEAD(hl_irqs);
static rwlock_t hl_irqs_lock = RW_LOCK_UNLOCKED;

static LIST_HEAD(addr_space);
static rwlock_t addr_space_lock = RW_LOCK_UNLOCKED;

/* addr_space list will have zero and max already included as bounds */
static struct hpsb_address_ops dummy_ops = { NULL, NULL, NULL, NULL };
static struct hpsb_address_serve dummy_zero_addr, dummy_max_addr;


static struct hl_host_info *hl_get_hostinfo(struct hpsb_highlevel *hl,
					      struct hpsb_host *host)
{
	struct hl_host_info *hi = NULL;
	struct list_head *lh;

	if (!hl || !host)
		return NULL;

	read_lock(&hl->host_info_lock);
	list_for_each (lh, &hl->host_info_list) {
		hi = list_entry(lh, struct hl_host_info, list);
		if (hi->host == host)
			break;
		hi = NULL;
	}
	read_unlock(&hl->host_info_lock);

	return hi;
}


/* Returns a per host/driver data structure that was previously stored by
 * hpsb_create_hostinfo. */
void *hpsb_get_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host)
{
	struct hl_host_info *hi = hl_get_hostinfo(hl, host);

	if (hi)
		return hi->data;

	return NULL;
}


/* If size is zero, then the return here is only valid for error checking */
void *hpsb_create_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host,
			   size_t data_size)
{
	struct hl_host_info *hi;
	void *data;
	unsigned long flags;

	hi = hl_get_hostinfo(hl, host);
	if (hi) {
		HPSB_ERR("%s called hpsb_create_hostinfo when hostinfo already exists",
			 hl->name);
		return NULL;
	}

	hi = kmalloc(sizeof(*hi) + data_size, GFP_ATOMIC);
	if (!hi)
		return NULL;

	memset(hi, 0, sizeof(*hi) + data_size);

	if (data_size) {
		data = hi->data = hi + 1;
		hi->size = data_size;
	} else
		data = hi;

	hi->host = host;

	write_lock_irqsave(&hl->host_info_lock, flags);
	list_add_tail(&hi->list, &hl->host_info_list);
	write_unlock_irqrestore(&hl->host_info_lock, flags);

	return data;
}


int hpsb_set_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host,
		      void *data)
{
	struct hl_host_info *hi;

	hi = hl_get_hostinfo(hl, host);
	if (hi) {
		if (!hi->size && !hi->data) {
			hi->data = data;
			return 0;
		} else
			HPSB_ERR("%s called hpsb_set_hostinfo when hostinfo already has data",
				 hl->name);
	} else
		HPSB_ERR("%s called hpsb_set_hostinfo when no hostinfo exists",
			 hl->name);

	return -EINVAL;
}


void hpsb_destroy_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host)
{
	struct hl_host_info *hi;

	hi = hl_get_hostinfo(hl, host);
	if (hi) {
		unsigned long flags;
		write_lock_irqsave(&hl->host_info_lock, flags);
		list_del(&hi->list);
		write_unlock_irqrestore(&hl->host_info_lock, flags);
		kfree(hi);
	}

	return;
}


void hpsb_set_hostinfo_key(struct hpsb_highlevel *hl, struct hpsb_host *host, unsigned long key)
{
	struct hl_host_info *hi;

	hi = hl_get_hostinfo(hl, host);
	if (hi)
		hi->key = key;

	return;
}


unsigned long hpsb_get_hostinfo_key(struct hpsb_highlevel *hl, struct hpsb_host *host)
{
	struct hl_host_info *hi;

	hi = hl_get_hostinfo(hl, host);
	if (hi)
		return hi->key;

	return 0;
}


void *hpsb_get_hostinfo_bykey(struct hpsb_highlevel *hl, unsigned long key)
{
	struct list_head *lh;
	struct hl_host_info *hi;
	void *data = NULL;

	if (!hl)
		return NULL;

	read_lock(&hl->host_info_lock);
	list_for_each (lh, &hl->host_info_list) {
		hi = list_entry(lh, struct hl_host_info, list);
		if (hi->key == key) {
			data = hi->data;
			break;
		}
	}
	read_unlock(&hl->host_info_lock);

	return data;
}


struct hpsb_host *hpsb_get_host_bykey(struct hpsb_highlevel *hl, unsigned long key)
{
	struct list_head *lh;
	struct hl_host_info *hi;
	struct hpsb_host *host = NULL;

	if (!hl)
		return NULL;

	read_lock(&hl->host_info_lock);
	list_for_each (lh, &hl->host_info_list) {
		hi = list_entry(lh, struct hl_host_info, list);
		if (hi->key == key) {
			host = hi->host;
			break;
		}
	}
	read_unlock(&hl->host_info_lock);

	return host;
}

static int highlevel_for_each_host_reg(struct hpsb_host *host, void *__data)
{
	struct hpsb_highlevel *hl = __data;

	hl->add_host(host);

	return 0;
}

void hpsb_register_highlevel(struct hpsb_highlevel *hl)
{
        INIT_LIST_HEAD(&hl->addr_list);
	INIT_LIST_HEAD(&hl->host_info_list);

	rwlock_init(&hl->host_info_lock);

	down_write(&hl_drivers_sem);
        list_add_tail(&hl->hl_list, &hl_drivers);
	up_write(&hl_drivers_sem);

	write_lock(&hl_irqs_lock);
	list_add_tail(&hl->irq_list, &hl_irqs);
	write_unlock(&hl_irqs_lock);

	if (hl->add_host)
		nodemgr_for_each_host(hl, highlevel_for_each_host_reg);

        return;
}

static int highlevel_for_each_host_unreg(struct hpsb_host *host, void *__data)
{
	struct hpsb_highlevel *hl = __data;

	hl->remove_host(host);
	hpsb_destroy_hostinfo(hl, host);

	return 0;
}

void hpsb_unregister_highlevel(struct hpsb_highlevel *hl)
{
	struct list_head *lh, *next;
        struct hpsb_address_serve *as;
	unsigned long flags;

	write_lock_irqsave(&addr_space_lock, flags);
	list_for_each_safe (lh, next, &hl->addr_list) {
                as = list_entry(lh, struct hpsb_address_serve, addr_list);
                list_del(&as->as_list);
                kfree(as);
        }
	write_unlock_irqrestore(&addr_space_lock, flags);

	write_lock(&hl_irqs_lock);
	list_del(&hl->irq_list);
	write_unlock(&hl_irqs_lock);

	down_write(&hl_drivers_sem);
        list_del(&hl->hl_list);
	up_write(&hl_drivers_sem);

        if (hl->remove_host)
		nodemgr_for_each_host(hl, highlevel_for_each_host_unreg);
}

int hpsb_register_addrspace(struct hpsb_highlevel *hl, struct hpsb_host *host,
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
        entry = host->addr_space.next;

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
        write_unlock_irqrestore(&addr_space_lock, flags);

        if (retval == 0) {
                kfree(as);
        }

        return retval;
}

int hpsb_unregister_addrspace(struct hpsb_highlevel *hl, struct hpsb_host *host,
                              u64 start)
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
                if (as->start == start && as->host == host) {
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

static void init_hpsb_highlevel(struct hpsb_host *host)
{
	INIT_LIST_HEAD(&dummy_zero_addr.as_list);
	INIT_LIST_HEAD(&dummy_zero_addr.addr_list);
	INIT_LIST_HEAD(&dummy_max_addr.as_list);
	INIT_LIST_HEAD(&dummy_max_addr.addr_list);

	dummy_zero_addr.op = dummy_max_addr.op = &dummy_ops;

	dummy_zero_addr.start = dummy_zero_addr.end = 0;
	dummy_max_addr.start = dummy_max_addr.end = ((u64) 1) << 48;

	list_add_tail(&dummy_zero_addr.as_list, &host->addr_space);
	list_add_tail(&dummy_max_addr.as_list, &host->addr_space);
}

void highlevel_add_host(struct hpsb_host *host)
{
        struct hpsb_highlevel *hl;

	init_hpsb_highlevel(host);

	down_read(&hl_drivers_sem);
        list_for_each_entry(hl, &hl_drivers, hl_list) {
		if (hl->add_host)
			hl->add_host(host);
        }
	up_read(&hl_drivers_sem);
}

void highlevel_remove_host(struct hpsb_host *host)
{
        struct hpsb_highlevel *hl;
	struct list_head *lh, *next;
	struct hpsb_address_serve *as;
	unsigned long flags;

	down_read(&hl_drivers_sem);
	list_for_each_entry(hl, &hl_drivers, hl_list) {
		if (hl->remove_host) {
			hl->remove_host(host);
			hpsb_destroy_hostinfo(hl, host);
		}
        }
	up_read(&hl_drivers_sem);

	/* Free up 1394 address space left behind by high level drivers. */
	write_lock_irqsave(&addr_space_lock, flags);
	list_for_each_safe (lh, next, &host->addr_space) {
		as = list_entry(lh, struct hpsb_address_serve, as_list);
		if (!list_empty(&as->addr_list)) {
			list_del(&as->addr_list);
			kfree(as);
		}
	}
	write_unlock_irqrestore(&addr_space_lock, flags);
}

void highlevel_host_reset(struct hpsb_host *host)
{
        struct hpsb_highlevel *hl;

	read_lock(&hl_irqs_lock);
	list_for_each_entry(hl, &hl_irqs, irq_list) {
                if (hl->host_reset)
                        hl->host_reset(host);
        }
	read_unlock(&hl_irqs_lock);
}

void highlevel_iso_receive(struct hpsb_host *host, void *data, size_t length)
{
        struct hpsb_highlevel *hl;
        int channel = (((quadlet_t *)data)[0] >> 8) & 0x3f;

        read_lock(&hl_irqs_lock);
	list_for_each_entry(hl, &hl_irqs, irq_list) {
                if (hl->iso_receive)
                        hl->iso_receive(host, channel, data, length);
        }
        read_unlock(&hl_irqs_lock);
}

void highlevel_fcp_request(struct hpsb_host *host, int nodeid, int direction,
			   void *data, size_t length)
{
        struct hpsb_highlevel *hl;
        int cts = ((quadlet_t *)data)[0] >> 4;

        read_lock(&hl_irqs_lock);
	list_for_each_entry(hl, &hl_irqs, irq_list) {
                if (hl->fcp_request)
                        hl->fcp_request(host, nodeid, direction, cts, data,
					length);
        }
        read_unlock(&hl_irqs_lock);
}

int highlevel_read(struct hpsb_host *host, int nodeid, void *data,
                   u64 addr, unsigned int length, u16 flags)
{
        struct hpsb_address_serve *as;
        struct list_head *entry;
        unsigned int partlength;
        int rcode = RCODE_ADDRESS_ERROR;

        read_lock(&addr_space_lock);

        entry = host->addr_space.next;
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

        entry = host->addr_space.next;
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

        entry = host->addr_space.next;
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

        entry = host->addr_space.next;
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

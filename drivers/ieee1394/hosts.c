/*
 * IEEE 1394 for Linux
 *
 * Low level (host adapter) management.
 *
 * Copyright (C) 1999 Andreas E. Bombe
 * Copyright (C) 1999 Emanuel Pirker
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/config.h>

#include <linux/types.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"


static LIST_HEAD(templates);
static spinlock_t templates_lock = SPIN_LOCK_UNLOCKED;

/*
 * This function calls the add_host/remove_host hooks for every host currently
 * registered.  Init == TRUE means add_host.
 */
void hl_all_hosts(struct hpsb_highlevel *hl, int init)
{
	struct list_head *tlh, *hlh;
        struct hpsb_host_template *tmpl;
        struct hpsb_host *host;

        spin_lock(&templates_lock);

	list_for_each(tlh, &templates) {
                tmpl = list_entry(tlh, struct hpsb_host_template, list);
		list_for_each(hlh, &tmpl->hosts) {
			host = list_entry(hlh, struct hpsb_host, list);
                        if (host->initialized) {
                                if (init) {
                                        if (hl->op->add_host) {
                                                hl->op->add_host(host);
                                        }
                                } else {
                                        if (hl->op->remove_host) {
                                                hl->op->remove_host(host);
                                        }
                                }
                        }
                }
        }

        spin_unlock(&templates_lock);
}

int hpsb_inc_host_usage(struct hpsb_host *host)
{
	struct list_head *tlh, *hlh;
        struct hpsb_host_template *tmpl;
        int retval = 0;
	unsigned long flags;

        spin_lock_irqsave(&templates_lock, flags);

	list_for_each(tlh, &templates) {
                tmpl = list_entry(tlh, struct hpsb_host_template, list);
		list_for_each(hlh, &tmpl->hosts) {
			if (host == list_entry(hlh, struct hpsb_host, list)) {
                                tmpl->devctl(host, MODIFY_USAGE, 1);
                                retval = 1;
                                break;
                        }
                }
		if (retval)
			break;
        }

        spin_unlock_irqrestore(&templates_lock, flags);

        return retval;
}

void hpsb_dec_host_usage(struct hpsb_host *host)
{
        host->template->devctl(host, MODIFY_USAGE, 0);
}

/*
 * The following function is exported for module usage.  It will be called from
 * the detect function of a adapter driver.
 */
struct hpsb_host *hpsb_get_host(struct hpsb_host_template *tmpl, 
                                size_t hd_size)
{
        struct hpsb_host *h;

        h = vmalloc(sizeof(struct hpsb_host) + hd_size);
        if (!h) return NULL;

        memset(h, 0, sizeof(struct hpsb_host) + hd_size);

        atomic_set(&h->generation, 0);

        INIT_LIST_HEAD(&h->pending_packets);
        spin_lock_init(&h->pending_pkt_lock);

        sema_init(&h->tlabel_count, 64);
        spin_lock_init(&h->tlabel_lock);

	INIT_TQUEUE(&h->timeout_tq, (void (*)(void*))abort_timedouts, h);

        h->topology_map = h->csr.topology_map + 3;
        h->speed_map = (u8 *)(h->csr.speed_map + 2);

        h->template = tmpl;
        if (hd_size)
                h->hostdata = &h->embedded_hostdata[0];

	list_add_tail(&h->list, &tmpl->hosts);

        return h;
}

static void free_all_hosts(struct hpsb_host_template *tmpl)
{
	struct list_head *hlh, *next;
        struct hpsb_host *host;

	list_for_each_safe(hlh, next, &tmpl->hosts) {
		host = list_entry(hlh, struct hpsb_host, list);
                vfree(host);
        }
}


static void init_hosts(struct hpsb_host_template *tmpl)
{
        int count;
	struct list_head *hlh;
        struct hpsb_host *host;

        count = tmpl->detect_hosts(tmpl);

	list_for_each(hlh, &tmpl->hosts) {
		host = list_entry(hlh, struct hpsb_host, list);
                if (tmpl->initialize_host(host)) {
                        host->initialized = 1;

                        highlevel_add_host(host);
                        hpsb_reset_bus(host, LONG_RESET);
                }
        }

        tmpl->number_of_hosts = count;
        HPSB_INFO("detected %d %s adapter%s", count, tmpl->name,
                  (count != 1 ? "s" : ""));
}

static void shutdown_hosts(struct hpsb_host_template *tmpl)
{
	struct list_head *hlh;
        struct hpsb_host *host;

	list_for_each(hlh, &tmpl->hosts) {
		host = list_entry(hlh, struct hpsb_host, list);
                if (host->initialized) {
                        host->initialized = 0;
                        abort_requests(host);

                        highlevel_remove_host(host);

                        tmpl->release_host(host);
                        while (test_bit(0, &host->timeout_tq.sync)) {
                                schedule();
                        }
                }
        }
        free_all_hosts(tmpl);
        tmpl->release_host(NULL);

        tmpl->number_of_hosts = 0;
}


/*
 * The following two functions are exported symbols for module usage.
 */
int hpsb_register_lowlevel(struct hpsb_host_template *tmpl)
{
	INIT_LIST_HEAD(&tmpl->hosts);
	tmpl->number_of_hosts = 0;

        spin_lock(&templates_lock);
	list_add_tail(&tmpl->list, &templates);
        spin_unlock(&templates_lock);

	/* PCI cards should be smart and use the PCI detection layer, and
	 * not this one shot deal. detect_hosts() will be obsoleted soon. */
	if (tmpl->detect_hosts != NULL) {
		HPSB_DEBUG("Registered %s driver, initializing now", tmpl->name);
		init_hosts(tmpl);
	}

        return 0;
}

void hpsb_unregister_lowlevel(struct hpsb_host_template *tmpl)
{
        shutdown_hosts(tmpl);

        if (tmpl->number_of_hosts)
                HPSB_PANIC("attempted to remove busy host template "
			   "of %s at address 0x%p", tmpl->name, tmpl);
	else {
		spin_lock(&templates_lock);
		list_del(&tmpl->list);
		spin_unlock(&templates_lock);
	}
}

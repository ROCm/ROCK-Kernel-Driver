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


static struct hpsb_host_template *templates = NULL;
spinlock_t templates_lock = SPIN_LOCK_UNLOCKED;

/*
 * This function calls the add_host/remove_host hooks for every host currently
 * registered.  Init == TRUE means add_host.
 */
void hl_all_hosts(struct hpsb_highlevel *hl, int init)
{
        struct hpsb_host_template *tmpl;
        struct hpsb_host *host;

        spin_lock(&templates_lock);

        for (tmpl = templates; tmpl != NULL; tmpl = tmpl->next) {
                for (host = tmpl->hosts; host != NULL; host = host->next) {
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
        struct hpsb_host_template *tmpl;
        struct hpsb_host *h;
        int retval = 0;
	unsigned long flags;

        spin_lock_irqsave(&templates_lock, flags);

        for (tmpl = templates; (tmpl != NULL) && !retval; tmpl = tmpl->next) {
                for (h = tmpl->hosts; h != NULL; h = h->next) {
                        if (h == host) {
                                tmpl->devctl(h, MODIFY_USAGE, 1);
                                retval = 1;
                                break;
                        }
                }
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
        if (h == NULL) {
                return NULL;
        }

        memset(h, 0, sizeof(struct hpsb_host) + hd_size);
        INIT_LIST_HEAD(&h->pending_packets);
        spin_lock_init(&h->pending_pkt_lock);

        sema_init(&h->tlabel_count, 64);
        spin_lock_init(&h->tlabel_lock);

        h->timeout_tq.routine = (void (*)(void*))abort_timedouts;
        h->timeout_tq.data = h;

        h->topology_map = h->csr.topology_map + 3;
        h->speed_map = (u8 *)(h->csr.speed_map + 2);

        h->template = tmpl;
        if (hd_size) {
                h->hostdata = &h->embedded_hostdata[0];
        }

        if (tmpl->hosts == NULL) {
                tmpl->hosts = h;
        } else {
                struct hpsb_host *last = tmpl->hosts;

                while (last->next != NULL) {
                        last = last->next;
                }
                last->next = h;
        }

        return h;
}

static void free_all_hosts(struct hpsb_host_template *tmpl)
{
        struct hpsb_host *next, *host = tmpl->hosts;

        while (host) {
                next = host->next;
                vfree(host);
                host = next;
        }
}


static void init_hosts(struct hpsb_host_template *tmpl)
{
        int count;
        struct hpsb_host *host;

        count = tmpl->detect_hosts(tmpl);

        for (host = tmpl->hosts; host != NULL; host = host->next) {
                if (tmpl->initialize_host(host)) {
                        host->initialized = 1;

                        highlevel_add_host(host);
                        hpsb_reset_bus(host);
                }
        }

        tmpl->number_of_hosts = count;
        HPSB_INFO("detected %d %s adapter%c", count, tmpl->name,
                  (count != 1 ? 's' : ' '));
}

static void shutdown_hosts(struct hpsb_host_template *tmpl)
{
        struct hpsb_host *host;

        for (host = tmpl->hosts; host != NULL; host = host->next) {
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


static int add_template(struct hpsb_host_template *new)
{
        new->next = NULL;
        new->hosts = NULL;
        new->number_of_hosts = 0;

        spin_lock(&templates_lock);
        if (templates == NULL) {
                templates = new;
        } else {
                struct hpsb_host_template *last = templates;
                while (last->next != NULL) {
                        last = last->next;
                }
                last->next = new;
        }
        spin_unlock(&templates_lock);

        return 0;
}

static int remove_template(struct hpsb_host_template *tmpl)
{
        int retval = 0;

        if (tmpl->number_of_hosts) {
                HPSB_ERR("attempted to remove busy host template "
                         "of %s at address 0x%p", tmpl->name, tmpl);
                return 1;
        }

        spin_lock(&templates_lock);
        if (templates == tmpl) {
                templates = tmpl->next;
        } else {
                struct hpsb_host_template *t;

                t = templates;
                while (t->next != tmpl && t->next != NULL) {
                        t = t->next;
                }

                if (t->next == NULL) {
                        HPSB_ERR("attempted to remove unregistered host template "
                                 "of %s at address 0x%p", tmpl->name, tmpl);
                        retval = -1;
                } else {
                        t->next = tmpl->next;
                }
        }
        spin_unlock(&templates_lock);

        inc_hpsb_generation();
        return retval;
}


/*
 * The following two functions are exported symbols for module usage.
 */
int hpsb_register_lowlevel(struct hpsb_host_template *tmpl)
{
        add_template(tmpl);
        HPSB_INFO("registered %s driver, initializing now", tmpl->name);
        init_hosts(tmpl);

        return 0;
}

void hpsb_unregister_lowlevel(struct hpsb_host_template *tmpl)
{
        shutdown_hosts(tmpl);

        if (remove_template(tmpl)) {
                HPSB_PANIC("remove_template failed on %s", tmpl->name);
        }
}



#ifndef MODULE

/*
 * This is the init function for builtin lowlevel drivers.  To add new drivers
 * put their setup code (get and register template) here.  Module only
 * drivers don't need to touch this.
 */

#define SETUP_TEMPLATE(name, visname) \
do {                                                                       \
        extern struct hpsb_host_template *get_ ## name ## _template(void); \
        t = get_ ## name ## _template();                                   \
                                                                           \
        if (t != NULL) {                                                   \
                if(!hpsb_register_lowlevel(t)) {                           \
                        count++;                                           \
                }                                                          \
        } else {                                                           \
                HPSB_WARN(visname " driver returned no host template");    \
        }                                                                  \
} while (0)

void __init register_builtin_lowlevels()
{
        struct hpsb_host_template *t;
        int count = 0;

        /* Touch t to avoid warning if no drivers are configured to
         * be built directly into the kernel. */
        t = NULL;

#ifdef CONFIG_IEEE1394_PCILYNX
        SETUP_TEMPLATE(lynx, "Lynx");
#endif

#ifdef CONFIG_IEEE1394_AIC5800
        SETUP_TEMPLATE(aic, "AIC-5800");
#endif
 
#ifdef CONFIG_IEEE1394_OHCI1394
        SETUP_TEMPLATE(ohci, "OHCI-1394");
#endif

        HPSB_INFO("%d host adapter%s initialized", count,
                  (count != 1 ? "s" : ""));
}

#undef SETUP_TEMPLATE

#endif /* !MODULE */

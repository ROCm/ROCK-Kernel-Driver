#ifndef _IEEE1394_HOSTS_H
#define _IEEE1394_HOSTS_H

#include <linux/wait.h>
#include <linux/tqueue.h>
#include <linux/list.h>
#include <asm/semaphore.h>

#include "ieee1394_types.h"
#include "csr.h"


struct hpsb_packet;

struct hpsb_host {
/* private fields (hosts, do not use them) */
	struct list_head list;

        atomic_t generation;

        struct list_head pending_packets;
        spinlock_t pending_pkt_lock;
        struct tq_struct timeout_tq;

        /* A bitmask where a set bit means that this tlabel is in use.
         * FIXME - should be handled per node instead of per bus. */
        u32 tlabel_pool[2];
        struct semaphore tlabel_count;
        spinlock_t tlabel_lock;

        int reset_retries;
        quadlet_t *topology_map;
        u8 *speed_map;
        struct csr_control csr;

        unsigned char iso_listen_count[64];

/* readonly fields for hosts */
        struct hpsb_host_template *template;

        int node_count; /* number of identified nodes on this bus */
        int selfid_count; /* total number of SelfIDs received */

        nodeid_t node_id; /* node ID of this host */
        nodeid_t irm_id; /* ID of this bus' isochronous resource manager */
        nodeid_t busmgr_id; /* ID of this bus' bus manager */

        unsigned initialized:1; /* initialized and usable */
        unsigned in_bus_reset:1; /* in bus reset / SelfID stage */
        unsigned attempt_root:1; /* attempt to become root during next reset */

        /* this nodes' duties on the bus */
        unsigned is_root:1;
        unsigned is_cycmst:1;
        unsigned is_irm:1;
        unsigned is_busmgr:1;

/* fields readable and writeable by the hosts */

        void *hostdata;
	struct pci_dev *pdev;
        int embedded_hostdata[0];
};



enum devctl_cmd {
        /* Host is requested to reset its bus and cancel all outstanding async
         * requests.  If arg == 1, it shall also attempt to become root on the
         * bus.  Return void. */
        RESET_BUS,

        /* Arg is void, return value is the hardware cycle counter value. */
        GET_CYCLE_COUNTER,

        /* Set the hardware cycle counter to the value in arg, return void.
         * FIXME - setting is probably not required. */
        SET_CYCLE_COUNTER,

        /* Configure hardware for new bus ID in arg, return void. */
        SET_BUS_ID,

        /* If arg true, start sending cycle start packets, stop if arg == 0.
         * Return void. */
        ACT_CYCLE_MASTER,

        /* Cancel all outstanding async requests without resetting the bus.
         * Return void. */
        CANCEL_REQUESTS,

        /* Decrease module usage count if arg == 0, increase otherwise.  Return
         * void. */
        MODIFY_USAGE,

        /* Start or stop receiving isochronous channel in arg.  Return void.
         * This acts as an optimization hint, hosts are not required not to
         * listen on unrequested channels. */
        ISO_LISTEN_CHANNEL,
        ISO_UNLISTEN_CHANNEL
};

enum reset_types {
        /* 166 microsecond reset -- only type of reset available on
           non-1394a capable IEEE 1394 controllers */
        LONG_RESET,

        /* Short (arbitrated) reset -- only available on 1394a capable
           IEEE 1394 capable controllers */
        SHORT_RESET
};

struct hpsb_host_template {
	struct list_head list;

        struct list_head hosts;
        int number_of_hosts;

        /* fields above will be ignored and overwritten after registering */

        /* This should be the name of the driver (single word) and must not be
         * NULL. */
        const char *name;

        /* This function shall detect all available adapters of this type and
         * call hpsb_get_host for each one.  The initialize_host function will
         * be called to actually set up these adapters.  The number of detected
         * adapters or zero if there are none must be returned.
         */
        int (*detect_hosts) (struct hpsb_host_template *template);

        /* After detecting and registering hosts, this function will be called
         * for every registered host.  It shall set up the host to be fully
         * functional for bus operations and return 0 for failure.
         */
        int (*initialize_host) (struct hpsb_host *host);

        /* To unload modules, this function is provided.  It shall free all
         * resources this host is using (if host is not NULL) or free all
         * resources globally allocated by the driver (if host is NULL).
         */
        void (*release_host) (struct hpsb_host *host); 

        /* This function must store a pointer to the configuration ROM into the
         * location referenced to by pointer and return the size of the ROM. It
         * may not fail.  If any allocation is required, it must be done
         * earlier.
         */
        size_t (*get_rom) (struct hpsb_host *host, const quadlet_t **pointer);

        /* This function shall implement packet transmission based on
         * packet->type.  It shall CRC both parts of the packet (unless
         * packet->type == raw) and do byte-swapping as necessary or instruct
         * the hardware to do so.  It can return immediately after the packet
         * was queued for sending.  After sending, hpsb_sent_packet() has to be
         * called.  Return 0 for failure.
         * NOTE: The function must be callable in interrupt context.
         */
        int (*transmit_packet) (struct hpsb_host *host, 
                                struct hpsb_packet *packet);

        /* This function requests miscellanous services from the driver, see
         * above for command codes and expected actions.  Return -1 for unknown
         * command, though that should never happen.
         */
        int (*devctl) (struct hpsb_host *host, enum devctl_cmd command, int arg);

        /* This function is mainly to redirect local CSR reads/locks to the iso
         * management registers (bus manager id, bandwidth available, channels
         * available) to the hardware registers in OHCI.  reg is 0,1,2,3 for bus
         * mgr, bwdth avail, ch avail hi, ch avail lo respectively (the same ids
         * as OHCI uses).  data and compare are the new data and expected data
         * respectively, return value is the old value.
         */
        quadlet_t (*hw_csr_reg) (struct hpsb_host *host, int reg,
                                 quadlet_t data, quadlet_t compare);
};



/* mid level internal use */
void register_builtin_lowlevels(void);

/* high level internal use */
struct hpsb_highlevel;
void hl_all_hosts(struct hpsb_highlevel *hl, int init);

/* 
 * These functions are for lowlevel (host) driver use.
 */
int hpsb_register_lowlevel(struct hpsb_host_template *tmpl);
void hpsb_unregister_lowlevel(struct hpsb_host_template *tmpl);

/*
 * Get a initialized host structure with hostdata_size bytes allocated in
 * embedded_hostdata for free usage.  Returns NULL for failure.  
 */
struct hpsb_host *hpsb_get_host(struct hpsb_host_template *tmpl, 
                                size_t hostdata_size);

/*
 * Increase / decrease host usage counter.  Increase function will return true
 * only if successful (host still existed).  Decrease function expects host to
 * exist.
 */
int hpsb_inc_host_usage(struct hpsb_host *host);
void hpsb_dec_host_usage(struct hpsb_host *host);

#endif /* _IEEE1394_HOSTS_H */

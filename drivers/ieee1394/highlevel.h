
#ifndef IEEE1394_HIGHLEVEL_H
#define IEEE1394_HIGHLEVEL_H


struct hpsb_highlevel {
        struct list_head hl_list;

        /* List of hpsb_address_serve. */
        struct list_head addr_list;

        const char *name;
        struct hpsb_highlevel_ops *op;
};


struct hpsb_address_serve {
        struct list_head as_list; /* global list */
        
        struct list_head addr_list; /* hpsb_highlevel list */

        struct hpsb_address_ops *op;

        /* first address handled and first address behind, quadlet aligned */
        u64 start, end;
};


/*
 * The above structs are internal to highlevel driver handling.  Only the
 * following structures are of interest to actual highlevel drivers.  
 */

struct hpsb_highlevel_ops {
        /* Any of the following pointers can legally be NULL, except for
         * iso_receive which can only be NULL when you don't request
         * channels. */

        /* New host initialized.  Will also be called during
         * hpsb_register_highlevel for all hosts already installed. */
        void (*add_host) (struct hpsb_host *host);

        /* Host about to be removed.  Will also be called during
         * hpsb_unregister_highlevel once for each host. */
        void (*remove_host) (struct hpsb_host *host);

        /* Host experienced bus reset with possible configuration changes.  Note
         * that this one may occur during interrupt/bottom half handling.  You
         * can not expect to be able to do stock hpsb_reads. */
        void (*host_reset) (struct hpsb_host *host);

        /* An isochronous packet was received.  Channel contains the channel
         * number for your convenience, it is also contained in the included
         * packet header (first quadlet, CRCs are missing).  You may get called
         * for channel/host combinations you did not request. */
        void (*iso_receive) (struct hpsb_host *host, int channel,
                             quadlet_t *data, unsigned int length);

        /* A write request was received on either the FCP_COMMAND (direction =
         * 0) or the FCP_RESPONSE (direction = 1) register.  The cts arg
         * contains the cts field (first byte of data).
         */
        void (*fcp_request) (struct hpsb_host *host, int nodeid, int direction,
                             int cts, u8 *data, unsigned int length);
};

struct hpsb_address_ops {
        /*
         * Null function pointers will make the respective operation complete 
         * with RCODE_TYPE_ERROR.  Makes for easy to implement read-only 
         * registers (just leave everything but read NULL).
         *
         * All functions shall return appropriate IEEE 1394 rcodes.
         */

        /* These functions have to implement block reads for themselves. */
        int (*read) (struct hpsb_host *host, int nodeid, quadlet_t *buffer,
                     u64 addr, unsigned int length);
        int (*write) (struct hpsb_host *host, int nodeid, int destid,
		      quadlet_t *data, u64 addr, unsigned int length);

        /* Lock transactions: write results of ext_tcode operation into
         * *store. */
        int (*lock) (struct hpsb_host *host, int nodeid, quadlet_t *store,
                     u64 addr, quadlet_t data, quadlet_t arg, int ext_tcode);
        int (*lock64) (struct hpsb_host *host, int nodeid, octlet_t *store,
                       u64 addr, octlet_t data, octlet_t arg, int ext_tcode);
};


void init_hpsb_highlevel(void);

void highlevel_add_host(struct hpsb_host *host);
void highlevel_remove_host(struct hpsb_host *host);
void highlevel_host_reset(struct hpsb_host *host);

int highlevel_read(struct hpsb_host *host, int nodeid, quadlet_t *buffer,
                   u64 addr, unsigned int length);
int highlevel_write(struct hpsb_host *host, int nodeid, int destid,
		    quadlet_t *data, u64 addr, unsigned int length);
int highlevel_lock(struct hpsb_host *host, int nodeid, quadlet_t *store,
                   u64 addr, quadlet_t data, quadlet_t arg, int ext_tcode);
int highlevel_lock64(struct hpsb_host *host, int nodeid, octlet_t *store,
                     u64 addr, octlet_t data, octlet_t arg, int ext_tcode);

void highlevel_iso_receive(struct hpsb_host *host, quadlet_t *data,
                           unsigned int length);
void highlevel_fcp_request(struct hpsb_host *host, int nodeid, int direction,
                           u8 *data, unsigned int length);


/*
 * Register highlevel driver.  The name pointer has to stay valid at all times
 * because the string is not copied.
 */
struct hpsb_highlevel *hpsb_register_highlevel(const char *name,
                                               struct hpsb_highlevel_ops *ops);
void hpsb_unregister_highlevel(struct hpsb_highlevel *hl);

/*
 * Register handlers for host address spaces.  Start and end are 48 bit pointers
 * and have to be quadlet aligned (end points to the first address behind the
 * handled addresses.  This function can be called multiple times for a single
 * hpsb_highlevel to implement sparse register sets.  The requested region must
 * not overlap any previously allocated region, otherwise registering will fail.
 *
 * It returns true for successful allocation.  There is no unregister function,
 * all address spaces are deallocated together with the hpsb_highlevel.
 */
int hpsb_register_addrspace(struct hpsb_highlevel *hl,
                            struct hpsb_address_ops *ops, u64 start, u64 end);

/*
 * Enable or disable receving a certain isochronous channel through the
 * iso_receive op.
 */
void hpsb_listen_channel(struct hpsb_highlevel *hl, struct hpsb_host *host, 
                         unsigned int channel);
void hpsb_unlisten_channel(struct hpsb_highlevel *hl, struct hpsb_host *host,
                           unsigned int channel);

#endif /* IEEE1394_HIGHLEVEL_H */

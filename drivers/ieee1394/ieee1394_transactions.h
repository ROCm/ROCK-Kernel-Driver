#ifndef _IEEE1394_TRANSACTIONS_H
#define _IEEE1394_TRANSACTIONS_H

#include "ieee1394_core.h"


/*
 * Utility functions to fill out packet headers.
 */
void fill_async_readquad(struct hpsb_packet *packet, u64 addr);
void fill_async_readquad_resp(struct hpsb_packet *packet, int rcode, 
                              quadlet_t data);
void fill_async_readblock(struct hpsb_packet *packet, u64 addr, int length);
void fill_async_readblock_resp(struct hpsb_packet *packet, int rcode, 
                               int length);
void fill_async_writequad(struct hpsb_packet *packet, u64 addr, quadlet_t data);
void fill_async_writeblock(struct hpsb_packet *packet, u64 addr, int length);
void fill_async_write_resp(struct hpsb_packet *packet, int rcode);
void fill_async_lock(struct hpsb_packet *packet, u64 addr, int extcode, 
                     int length);
void fill_async_lock_resp(struct hpsb_packet *packet, int rcode, int extcode, 
                          int length);
void fill_iso_packet(struct hpsb_packet *packet, int length, int channel,
                     int tag, int sync);

/*
 * Get and free transaction labels.
 */
int get_tlabel(struct hpsb_host *host, nodeid_t nodeid, int wait);
void free_tlabel(struct hpsb_host *host, nodeid_t nodeid, int tlabel);

struct hpsb_packet *hpsb_make_readqpacket(struct hpsb_host *host, nodeid_t node,
                                          u64 addr);
struct hpsb_packet *hpsb_make_readbpacket(struct hpsb_host *host, nodeid_t node,
                                          u64 addr, size_t length);
struct hpsb_packet *hpsb_make_writeqpacket(struct hpsb_host *host,
                                           nodeid_t node, u64 addr,
                                           quadlet_t data);
struct hpsb_packet *hpsb_make_writebpacket(struct hpsb_host *host,
                                           nodeid_t node, u64 addr,
                                           size_t length);
struct hpsb_packet *hpsb_make_lockpacket(struct hpsb_host *host, nodeid_t node,
                                         u64 addr, int extcode);


/*
 * hpsb_packet_success - Make sense of the ack and reply codes and
 * return more convenient error codes:
 * 0           success
 * -EBUSY      node is busy, try again
 * -EAGAIN     error which can probably resolved by retry
 * -EREMOTEIO  node suffers from an internal error
 * -EACCES     this transaction is not allowed on requested address
 * -EINVAL     invalid address at node
 */
int hpsb_packet_success(struct hpsb_packet *packet);


/*
 * The generic read, write and lock functions.  All recognize the local node ID
 * and act accordingly.  Read and write automatically use quadlet commands if
 * length == 4 and and block commands otherwise (however, they do not yet
 * support lengths that are not a multiple of 4).
 */
int hpsb_read(struct hpsb_host *host, nodeid_t node, u64 addr,
              quadlet_t *buffer, size_t length);
int hpsb_write(struct hpsb_host *host, nodeid_t node, u64 addr,
               quadlet_t *buffer, size_t length);
int hpsb_lock(struct hpsb_host *host, nodeid_t node, u64 addr, int extcode,
              quadlet_t *data, quadlet_t arg);

#endif /* _IEEE1394_TRANSACTIONS_H */

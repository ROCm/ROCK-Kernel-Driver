/*
 * IEEE 1394 for Linux
 *
 * CSR implementation, iso/bus manager implementation.
 *
 * Copyright (C) 1999 Andreas E. Bombe
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/string.h>

#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394.h"
#include "highlevel.h"


/* FIXME: this one won't work on little endian with big endian data */
static u16 csr_crc16(unsigned *data, int length)
{
        int check=0, i;
        int shift, sum, next=0;

        for (i = length; i; i--) {
                for (next = check, shift = 28; shift >= 0; shift -= 4 ) {
                        sum = ((next >> 12) ^ (*data >> shift)) & 0xf;
                        next = (next << 4) ^ (sum << 12) ^ (sum << 5) ^ (sum);
                }
                check = next & 0xffff;
                data++;
        }

        return check;
}

static void host_reset(struct hpsb_host *host)
{
        host->csr.state &= 0x300;

        host->csr.bus_manager_id = 0x3f;
        host->csr.bandwidth_available = 4915;
        host->csr.channels_available_hi = ~0;
        host->csr.channels_available_lo = ~0;

        host->csr.node_ids = host->node_id << 16;

        if (!host->is_root) {
                /* clear cmstr bit */
                host->csr.state &= ~0x100;
        }

        host->csr.topology_map[1] = 
                cpu_to_be32(be32_to_cpu(host->csr.topology_map[1]) + 1);
        host->csr.topology_map[2] = cpu_to_be32(host->node_count << 16 
                                                | host->selfid_count);
        host->csr.topology_map[0] = 
                cpu_to_be32((host->selfid_count + 2) << 16
                            | csr_crc16(host->csr.topology_map + 1,
                                        host->selfid_count + 2));

        host->csr.speed_map[0] = cpu_to_be32(0x3f1 << 16 
                                             | csr_crc16(host->csr.speed_map+1,
                                                         0x3f1));
}


static void add_host(struct hpsb_host *host)
{
        host->csr.lock = SPIN_LOCK_UNLOCKED;

        host->csr.rom_size = host->template->get_rom(host, &host->csr.rom);

        host->csr.state                 = 0;
        host->csr.node_ids              = 0;
        host->csr.split_timeout_hi      = 0;
        host->csr.split_timeout_lo      = 800 << 19;
        host->csr.cycle_time            = 0;
        host->csr.bus_time              = 0;
        host->csr.bus_manager_id        = 0x3f;
        host->csr.bandwidth_available   = 4915;
        host->csr.channels_available_hi = ~0;
        host->csr.channels_available_lo = ~0;
}


/* Read topology / speed maps and configuration ROM */
static int read_maps(struct hpsb_host *host, int nodeid, quadlet_t *buffer,
                     u64 addr, unsigned int length)
{
        int csraddr = addr - CSR_REGISTER_BASE;
        const char *src;

        if (csraddr < CSR_TOPOLOGY_MAP) {
                if (csraddr + length > CSR_CONFIG_ROM + host->csr.rom_size) {
                        return RCODE_ADDRESS_ERROR;
                }
                src = ((char *)host->csr.rom) + csraddr - CSR_CONFIG_ROM;
        } else if (csraddr < CSR_SPEED_MAP) {
                src = ((char *)host->csr.topology_map) + csraddr 
                        - CSR_TOPOLOGY_MAP;
        } else {
                src = ((char *)host->csr.speed_map) + csraddr - CSR_SPEED_MAP;
        }

        memcpy(buffer, src, length);
        return RCODE_COMPLETE;
}


#define out if (--length == 0) break

static int read_regs(struct hpsb_host *host, int nodeid, quadlet_t *buf,
                     u64 addr, unsigned int length)
{
        int csraddr = addr - CSR_REGISTER_BASE;
        int oldcycle;
        quadlet_t ret;
        
        if ((csraddr | length) & 0x3)
                return RCODE_TYPE_ERROR;

        length /= 4;

        switch (csraddr) {
        case CSR_STATE_CLEAR:
                *(buf++) = cpu_to_be32(host->csr.state);
                out;
        case CSR_STATE_SET:
                *(buf++) = cpu_to_be32(host->csr.state);
                out;
        case CSR_NODE_IDS:
                *(buf++) = cpu_to_be32(host->csr.node_ids);
                out;

        case CSR_RESET_START:
                return RCODE_TYPE_ERROR;

                /* address gap - handled by default below */

        case CSR_SPLIT_TIMEOUT_HI:
                *(buf++) = cpu_to_be32(host->csr.split_timeout_hi);
                out;
        case CSR_SPLIT_TIMEOUT_LO:
                *(buf++) = cpu_to_be32(host->csr.split_timeout_lo);
                out;

                /* address gap */
                return RCODE_ADDRESS_ERROR;

        case CSR_CYCLE_TIME:
                oldcycle = host->csr.cycle_time;
                host->csr.cycle_time =
                        host->template->devctl(host, GET_CYCLE_COUNTER, 0);

                if (oldcycle > host->csr.cycle_time) {
                        /* cycle time wrapped around */
                        host->csr.bus_time += 1 << 7;
                }
                *(buf++) = cpu_to_be32(host->csr.cycle_time);
                out;
        case CSR_BUS_TIME:
                oldcycle = host->csr.cycle_time;
                host->csr.cycle_time =
                        host->template->devctl(host, GET_CYCLE_COUNTER, 0);

                if (oldcycle > host->csr.cycle_time) {
                        /* cycle time wrapped around */
                        host->csr.bus_time += (1 << 7);
                }
                *(buf++) = cpu_to_be32(host->csr.bus_time 
                                       | (host->csr.cycle_time >> 25));
                out;

                /* address gap */
                return RCODE_ADDRESS_ERROR;

        case CSR_BUSY_TIMEOUT:
                /* not yet implemented */
                return RCODE_ADDRESS_ERROR;

        case CSR_BUS_MANAGER_ID:
                if (host->template->hw_csr_reg)
                        ret = host->template->hw_csr_reg(host, 0, 0, 0);
                else
                        ret = host->csr.bus_manager_id;

                *(buf++) = cpu_to_be32(ret);
                out;
        case CSR_BANDWIDTH_AVAILABLE:
                if (host->template->hw_csr_reg)
                        ret = host->template->hw_csr_reg(host, 1, 0, 0);
                else
                        ret = host->csr.bandwidth_available;

                *(buf++) = cpu_to_be32(ret);
                out;
        case CSR_CHANNELS_AVAILABLE_HI:
                if (host->template->hw_csr_reg)
                        ret = host->template->hw_csr_reg(host, 2, 0, 0);
                else
                        ret = host->csr.channels_available_hi;

                *(buf++) = cpu_to_be32(ret);
                out;
        case CSR_CHANNELS_AVAILABLE_LO:
                if (host->template->hw_csr_reg)
                        ret = host->template->hw_csr_reg(host, 3, 0, 0);
                else
                        ret = host->csr.channels_available_lo;

                *(buf++) = cpu_to_be32(ret);
                out;

                /* address gap to end - fall through to default */
        default:
                return RCODE_ADDRESS_ERROR;
        }

        return RCODE_COMPLETE;
}

static int write_regs(struct hpsb_host *host, int nodeid, int destid,
		      quadlet_t *data, u64 addr, unsigned int length)
{
        int csraddr = addr - CSR_REGISTER_BASE;
        
        if ((csraddr | length) & 0x3)
                return RCODE_TYPE_ERROR;

        length /= 4;

        switch (csraddr) {
        case CSR_STATE_CLEAR:
                /* FIXME FIXME FIXME */
                printk("doh, someone wants to mess with state clear\n");
                out;
        case CSR_STATE_SET:
                printk("doh, someone wants to mess with state set\n");
                out;

        case CSR_NODE_IDS:
                host->csr.node_ids &= NODE_MASK << 16;
                host->csr.node_ids |= be32_to_cpu(*(data++)) & (BUS_MASK << 16);
                host->node_id = host->csr.node_ids >> 16;
                host->template->devctl(host, SET_BUS_ID, host->node_id >> 6);
                out;

        case CSR_RESET_START:
                /* FIXME - perform command reset */
                out;

                /* address gap */
                return RCODE_ADDRESS_ERROR;

        case CSR_SPLIT_TIMEOUT_HI:
                host->csr.split_timeout_hi = 
                        be32_to_cpu(*(data++)) & 0x00000007;
                out;
        case CSR_SPLIT_TIMEOUT_LO:
                host->csr.split_timeout_lo = 
                        be32_to_cpu(*(data++)) & 0xfff80000;
                out;

                /* address gap */
                return RCODE_ADDRESS_ERROR;

        case CSR_CYCLE_TIME:
                /* should only be set by cycle start packet, automatically */
                host->csr.cycle_time = be32_to_cpu(*data);
                host->template->devctl(host, SET_CYCLE_COUNTER,
                                       be32_to_cpu(*(data++)));
                out;
        case CSR_BUS_TIME:
                host->csr.bus_time = be32_to_cpu(*(data++)) & 0xffffff80;
                out;

                /* address gap */
                return RCODE_ADDRESS_ERROR;

        case CSR_BUSY_TIMEOUT:
                /* not yet implemented */
                return RCODE_ADDRESS_ERROR;

        case CSR_BUS_MANAGER_ID:
        case CSR_BANDWIDTH_AVAILABLE:
        case CSR_CHANNELS_AVAILABLE_HI:
        case CSR_CHANNELS_AVAILABLE_LO:
                /* these are not writable, only lockable */
                return RCODE_TYPE_ERROR;

                /* address gap to end - fall through */
        default:
                return RCODE_ADDRESS_ERROR;
        }

        return RCODE_COMPLETE;
}

#undef out


static int lock_regs(struct hpsb_host *host, int nodeid, quadlet_t *store,
                     u64 addr, quadlet_t data, quadlet_t arg, int extcode)
{
        int csraddr = addr - CSR_REGISTER_BASE;
        unsigned long flags;
        quadlet_t *regptr = NULL;

        if (csraddr & 0x3)
		return RCODE_TYPE_ERROR;

        if (csraddr < CSR_BUS_MANAGER_ID || csraddr > CSR_CHANNELS_AVAILABLE_LO
            || extcode != EXTCODE_COMPARE_SWAP)
                goto unsupported_lockreq;

        data = be32_to_cpu(data);
        arg = be32_to_cpu(arg);

        if (host->template->hw_csr_reg) {
                quadlet_t old;

                old = host->template->
                        hw_csr_reg(host, (csraddr - CSR_BUS_MANAGER_ID) >> 2,
                                   data, arg);

                *store = cpu_to_be32(old);
                return RCODE_COMPLETE;
        }

        spin_lock_irqsave(&host->csr.lock, flags);

        switch (csraddr) {
        case CSR_BUS_MANAGER_ID:
                regptr = &host->csr.bus_manager_id;
                break;

        case CSR_BANDWIDTH_AVAILABLE:
                regptr = &host->csr.bandwidth_available;
                break;

        case CSR_CHANNELS_AVAILABLE_HI:
                regptr = &host->csr.channels_available_hi;
                break;

        case CSR_CHANNELS_AVAILABLE_LO:
                regptr = &host->csr.channels_available_lo;
                break;
        }

        *store = cpu_to_be32(*regptr);
        if (*regptr == arg) *regptr = data;
        spin_unlock_irqrestore(&host->csr.lock, flags);

        return RCODE_COMPLETE;

 unsupported_lockreq:
        switch (csraddr) {
        case CSR_STATE_CLEAR:
        case CSR_STATE_SET:
        case CSR_RESET_START:
        case CSR_NODE_IDS:
        case CSR_SPLIT_TIMEOUT_HI:
        case CSR_SPLIT_TIMEOUT_LO:
        case CSR_CYCLE_TIME:
        case CSR_BUS_TIME:
        case CSR_BUS_MANAGER_ID:
        case CSR_BANDWIDTH_AVAILABLE:
        case CSR_CHANNELS_AVAILABLE_HI:
        case CSR_CHANNELS_AVAILABLE_LO:
                return RCODE_TYPE_ERROR;

        case CSR_BUSY_TIMEOUT:
                /* not yet implemented - fall through */
        default:
                return RCODE_ADDRESS_ERROR;
        }
}

static int write_fcp(struct hpsb_host *host, int nodeid, int dest,
		     quadlet_t *data, u64 addr, unsigned int length)
{
        int csraddr = addr - CSR_REGISTER_BASE;

        if (length > 512)
                return RCODE_TYPE_ERROR;

        switch (csraddr) {
        case CSR_FCP_COMMAND:
                highlevel_fcp_request(host, nodeid, 0, (u8 *)data, length);
                break;
        case CSR_FCP_RESPONSE:
                highlevel_fcp_request(host, nodeid, 1, (u8 *)data, length);
                break;
        default:
                return RCODE_TYPE_ERROR;
        }

        return RCODE_COMPLETE;
}


static struct hpsb_highlevel_ops csr_ops = {
        add_host: add_host,
        host_reset: host_reset,
};


static struct hpsb_address_ops map_ops = {
        read: read_maps,
};

static struct hpsb_address_ops fcp_ops = {
        write: write_fcp,
};

static struct hpsb_address_ops reg_ops = {
        read: read_regs,
        write: write_regs,
        lock: lock_regs,
};

static struct hpsb_highlevel *hl;

void init_csr(void)
{
        hl = hpsb_register_highlevel("standard registers", &csr_ops);
        if (hl == NULL) {
                HPSB_ERR("out of memory during ieee1394 initialization");
                return;
        }

        hpsb_register_addrspace(hl, &reg_ops, CSR_REGISTER_BASE,
                                CSR_REGISTER_BASE + CSR_CONFIG_ROM);
        hpsb_register_addrspace(hl, &map_ops, 
                                CSR_REGISTER_BASE + CSR_CONFIG_ROM,
                                CSR_REGISTER_BASE + CSR_CONFIG_ROM_END);
        hpsb_register_addrspace(hl, &fcp_ops,
                                CSR_REGISTER_BASE + CSR_FCP_COMMAND,
                                CSR_REGISTER_BASE + CSR_FCP_END);
        hpsb_register_addrspace(hl, &map_ops,
                                CSR_REGISTER_BASE + CSR_TOPOLOGY_MAP,
                                CSR_REGISTER_BASE + CSR_TOPOLOGY_MAP_END);
        hpsb_register_addrspace(hl, &map_ops,
                                CSR_REGISTER_BASE + CSR_SPEED_MAP,
                                CSR_REGISTER_BASE + CSR_SPEED_MAP_END);
}

void cleanup_csr(void)
{
        hpsb_unregister_highlevel(hl);
}

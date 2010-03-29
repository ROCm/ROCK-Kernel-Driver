/****************************************************************************
 * Copyright 2002-2005: Level 5 Networks Inc.
 * Copyright 2005-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications
 *  <linux-xen-drivers@solarflare.com>
 *  <onload-dev@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

/*
 * \author  djr
 *  \brief  Initialisation of VIs.
 *   \date  2007/06/08
 */

#include "ef_vi_internal.h"

#define EF_VI_STATE_BYTES(rxq_sz, txq_sz)			\
	(sizeof(ef_vi_state) + (rxq_sz) * sizeof(uint16_t)	\
	 + (txq_sz) * sizeof(uint16_t))

int ef_vi_calc_state_bytes(int rxq_sz, int txq_sz)
{
	ef_assert(rxq_sz == 0 || EF_VI_IS_POW2(rxq_sz));
	ef_assert(txq_sz == 0 || EF_VI_IS_POW2(txq_sz));

	return EF_VI_STATE_BYTES(rxq_sz, txq_sz);
}


int ef_vi_state_bytes(ef_vi* vi)
{
	int rxq_sz = 0, txq_sz = 0;
	if( ef_vi_receive_capacity(vi) )
		rxq_sz = ef_vi_receive_capacity(vi) + 1;
	if( ef_vi_transmit_capacity(vi) )
		txq_sz = ef_vi_transmit_capacity(vi) + 1;

	ef_assert(rxq_sz == 0 || EF_VI_IS_POW2(rxq_sz));
	ef_assert(txq_sz == 0 || EF_VI_IS_POW2(txq_sz));

	return EF_VI_STATE_BYTES(rxq_sz, txq_sz);
}


void ef_eventq_state_init(ef_vi* evq)
{
	int j;

	for (j = 0; j<EFAB_DMAQS_PER_EVQ_MAX; j++) {
		ef_rx_dup_state_t *rx_dup_state =
			&evq->evq_state->rx_dup_state[j];
		rx_dup_state->bad_sop = 0;
		rx_dup_state->rx_last_desc_ptr = -1;
		rx_dup_state->frag_num = 0;
	}

	evq->evq_state->evq_ptr = 0;
}


void ef_vi_state_init(ef_vi* vi)
{
	ef_vi_state* state = vi->ep_state;
	unsigned i;

	state->txq.added = state->txq.removed = 0;
	state->rxq.added = state->rxq.removed = 0;

	if( vi->vi_rxq.mask )
		for( i = 0; i <= vi->vi_rxq.mask; ++i )
			vi->vi_rxq.ids[i] = (uint16_t) -1;
	if( vi->vi_txq.mask )
		for( i = 0; i <= vi->vi_txq.mask; ++i )
			vi->vi_txq.ids[i] = (uint16_t) -1;
}


void ef_vi_init_mapping_evq(void* data_area, struct ef_vi_nic_type nic_type,
                            int instance, unsigned evq_bytes, void* base,
                            void* timer_reg)
{
	struct vi_mappings* vm = (struct vi_mappings*) data_area;

	vm->signature = VI_MAPPING_SIGNATURE;
	vm->vi_instance = instance;
	vm->nic_type = nic_type;
	vm->evq_bytes = evq_bytes;
	vm->evq_base = base;
	vm->evq_timer_reg = timer_reg;
}


void ef_vi_init(ef_vi* vi, void* vvis, ef_vi_state* state,
                ef_eventq_state* evq_state, enum ef_vi_flags vi_flags)
{
	struct vi_mappings* vm = (struct vi_mappings*) vvis;

	vi->vi_i = vm->vi_instance;
	vi->ep_state = state;
	vi->vi_flags = vi_flags;

	switch( vm->nic_type.arch ) {
	case EF_VI_ARCH_FALCON:
		falcon_vi_init(vi, vvis);
		break;
	default:
		/* ?? TODO: We should return an error code. */
		ef_assert(0);
		break;
	}

	if( vm->evq_bytes ) {
		vi->evq_state = evq_state;
		vi->evq_mask = vm->evq_bytes - 1u;
		vi->evq_base = vm->evq_base;
		vi->evq_timer_reg = vm->evq_timer_reg;
	}

	EF_VI_MAGIC_SET(vi, EF_VI);
}


/* Initialise [data_area] with information required to initialise an ef_vi.
 * In the following, an unused param should be set to NULL. Note the case
 * marked (*) of [iobuf_mmap] for falcon/driver; for the normal driver this
 * must be NULL.
 *
 * \param  data_area     [in,out] required, must ref at least VI_MAPPING_SIZE 
 *                                bytes
 * \param  io_mmap       [in] ef1,    required
 *                            falcon, required
 * \param  iobuf_mmap    [in] ef1,    unused
 *                            falcon, required
 */
void ef_vi_init_mapping_vi(void* data_area, struct ef_vi_nic_type nic_type,
                           unsigned rxq_capacity, unsigned txq_capacity,
                           int instance, void* io_mmap,
                           void* iobuf_mmap_rx, void* iobuf_mmap_tx,
                           enum ef_vi_flags vi_flags)
{
	struct vi_mappings* vm = (struct vi_mappings*) data_area;
	int rx_desc_bytes, rxq_bytes;

	ef_assert(rxq_capacity > 0 || txq_capacity > 0);
	ef_assert(vm);
	ef_assert(io_mmap);
	ef_assert(iobuf_mmap_rx || iobuf_mmap_tx);

	vm->signature = VI_MAPPING_SIGNATURE;
	vm->vi_instance = instance;
	vm->nic_type = nic_type;

	rx_desc_bytes = (vi_flags & EF_VI_RX_PHYS_ADDR) ? 8 : 4;
	rxq_bytes = rxq_capacity * rx_desc_bytes;
	rxq_bytes = (rxq_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

	if( iobuf_mmap_rx == iobuf_mmap_tx )
		iobuf_mmap_tx = (char*) iobuf_mmap_rx + rxq_bytes;

	vm->rx_queue_capacity = rxq_capacity;
	vm->rx_dma_falcon = iobuf_mmap_rx;
	vm->rx_bell       = (char*) io_mmap + (RX_DESC_UPD_REG_KER_OFST & 4095);
	vm->tx_queue_capacity = txq_capacity;
	vm->tx_dma_falcon = iobuf_mmap_tx;
	vm->tx_bell       = (char*) io_mmap + (TX_DESC_UPD_REG_KER_OFST & 4095);
}

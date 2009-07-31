/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains EtherFabric NIC hash algorithms implementation.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
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

#include <ci/efhw/debug.h>
#include <ci/driver/efab/hardware.h>


static unsigned int
common_get_ip_key(unsigned int src_ip, unsigned int src_port,
		  unsigned int dest_ip, unsigned int dest_port,
		  int tcp, int full, int tx, unsigned int masked_q_id)
{

	unsigned int tmp_port, result;

	EFHW_ASSERT(tcp == 0 || tcp == 1);
	EFHW_ASSERT(full == 0 || full == 1);
	EFHW_ASSERT(masked_q_id < (1 << 10));

	/* m=masked_q_id(TX)/0(RX)  u=UDP  S,D=src/dest addr  s,d=src/dest port
	 *
	 * Wildcard filters have src(TX)/dest(RX) addr and port = 0;
	 * and UDP wildcard filters have the src and dest port fields swapped.
	 *
	 * Addr/port fields are little-endian.
	 *
	 * 3322222222221111111111
	 * 10987654321098765432109876543210
	 *
	 * 000000000000000000000mmmmmmmmmmu ^
	 * DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD ^
	 * ddddddddddddddddSSSSSSSSSSSSSSSS ^
	 * SSSSSSSSSSSSSSSSssssssssssssssss
	 */

	if (!tx)
		masked_q_id = 0;

	if (!full) {
		if (tx) {
			dest_ip = 0;
			dest_port = 0;
		} else {
			src_ip = 0;
			src_port = 0;
		}
		if (!tcp) {
			tmp_port = src_port;
			src_port = dest_port;
			dest_port = tmp_port;
		}
	}

	result = ((masked_q_id << 1) | (!tcp))                              ^
		 (dest_ip)                                                  ^
		 (((dest_port & 0xffff) << 16) | ((src_ip >> 16) & 0xffff)) ^
		 (((src_ip & 0xffff) << 16) | (src_port & 0xffff));

	EFHW_TRACE("%s: IP %s %s %x", __func__, tcp ? "TCP" : "UDP",
		   full ? "Full" : "Wildcard", result);

	return result;
}


unsigned int
falcon_hash_get_ip_key(unsigned int src_ip, unsigned int src_port,
		       unsigned int dest_ip, unsigned int dest_port,
		       int tcp, int full)
{
	return common_get_ip_key(src_ip, src_port, dest_ip, dest_port, tcp,
				 full, 0, 0);
}


/* This function generates the First Hash key */
unsigned int falcon_hash_function1(unsigned int key, unsigned int nfilters)
{

	unsigned short int lfsr_reg;
	unsigned int tmp_key;
	int index;

	unsigned short int lfsr_input;
	unsigned short int single_bit_key;
	unsigned short int bit16_lfsr;
	unsigned short int bit3_lfsr;

	lfsr_reg = 0xFFFF;
	tmp_key = key;

	/* For Polynomial equation X^16+X^3+1 */
	for (index = 0; index < 32; index++) {
		/* Get the bit from key and shift the key */
		single_bit_key = (tmp_key & 0x80000000) >> 31;
		tmp_key = tmp_key << 1;

		/* get the Tap bits to XOR operation */
		bit16_lfsr = (lfsr_reg & 0x8000) >> 15;
		bit3_lfsr = (lfsr_reg & 0x0004) >> 2;

		/* Get the Input value to the LFSR */
		lfsr_input = ((bit16_lfsr ^ bit3_lfsr) ^ single_bit_key);

		/* Shift and store out of the two TAPs */
		lfsr_reg = lfsr_reg << 1;
		lfsr_reg = lfsr_reg | (lfsr_input & 0x0001);

	}

	lfsr_reg = lfsr_reg & (nfilters - 1);

	return lfsr_reg;
}

/* This function generates the Second Hash */
unsigned int
falcon_hash_function2(unsigned int key, unsigned int nfilters)
{
	return (unsigned int)(((unsigned long long)key * 2 - 1) &
			      (nfilters - 1));
}

/* This function iterates through the hash table */
unsigned int
falcon_hash_iterator(unsigned int hash1, unsigned int hash2,
		     unsigned int n_search, unsigned int nfilters)
{
	return (hash1 + (n_search * hash2)) & (nfilters - 1);
}


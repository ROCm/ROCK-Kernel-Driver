/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/addrs.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>


/*
 * Identification Register Access -- Read Only			    0000_0000 
 */
static uint64_t
__pcireg_id_get(pic_t *bridge)
{
	return bridge->p_wid_id;
}

uint64_t
pcireg_bridge_id_get(void *ptr)
{
	return __pcireg_id_get((pic_t *)ptr);
}

uint64_t
pcireg_id_get(pcibr_soft_t ptr)
{
	return __pcireg_id_get((pic_t *)ptr->bs_base);
}



/*
 * Address Bus Side Holding Register Access -- Read Only	    0000_0010
 */
uint64_t
pcireg_bus_err_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_wid_err;
}


/*
 * Control Register Access -- Read/Write			    0000_0020
 */
static uint64_t
__pcireg_control_get(pic_t *bridge)
{
	return bridge->p_wid_control;
}

uint64_t
pcireg_bridge_control_get(void *ptr)
{
	return __pcireg_control_get((pic_t *)ptr);
}

uint64_t
pcireg_control_get(pcibr_soft_t ptr)
{
	return __pcireg_control_get((pic_t *)ptr->bs_base);
}


void
pcireg_control_set(pcibr_soft_t ptr, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	/* WAR for PV 439897 & 454474.  Add a readback of the control 
	 * register.  Lock to protect against MP accesses to this
	 * register along with other write-only registers (See PVs).
	 * This register isnt accessed in the "hot path" so the splhi
	 * shouldn't be a bottleneck
	 */

	bridge->p_wid_control = val;
	bridge->p_wid_control;	/* WAR */
}


void
pcireg_control_bit_clr(pcibr_soft_t ptr, uint64_t bits)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	/* WAR for PV 439897 & 454474.  Add a readback of the control
	 * register.  Lock to protect against MP accesses to this
	 * register along with other write-only registers (See PVs).
	 * This register isnt accessed in the "hot path" so the splhi
	 * shouldn't be a bottleneck
	 */

	bridge->p_wid_control &= ~bits;
	bridge->p_wid_control;	/* WAR */
}


void
pcireg_control_bit_set(pcibr_soft_t ptr, uint64_t bits)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	/* WAR for PV 439897 & 454474.  Add a readback of the control
	 * register.  Lock to protect against MP accesses to this
	 * register along with other write-only registers (See PVs).
	 * This register isnt accessed in the "hot path" so the splhi
	 * shouldn't be a bottleneck
	 */

	bridge->p_wid_control |= bits;
	bridge->p_wid_control;	/* WAR */
}

/*
 * Bus Speed (from control register); -- Read Only access	    0000_0020
 * 0x00 == 33MHz, 0x01 == 66MHz, 0x10 == 100MHz, 0x11 == 133MHz
 */
uint64_t
pcireg_speed_get(pcibr_soft_t ptr)
{
	uint64_t speedbits;
	pic_t *bridge = (pic_t *)ptr->bs_base;

	speedbits = bridge->p_wid_control & PIC_CTRL_PCI_SPEED;
	return (speedbits >> 4);
}

/*
 * Bus Mode (ie. PCIX or PCI) (from Status register);		    0000_0008
 * 0x0 == PCI, 0x1 == PCI-X
 */
uint64_t
pcireg_mode_get(pcibr_soft_t ptr)
{
	uint64_t pcix_active_bit;
	pic_t *bridge = (pic_t *)ptr->bs_base;

	pcix_active_bit = bridge->p_wid_stat & PIC_STAT_PCIX_ACTIVE;
	return (pcix_active_bit >> PIC_STAT_PCIX_ACTIVE_SHFT);
}

void
pcireg_req_timeout_set(pcibr_soft_t ptr, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_wid_req_timeout = val;
}

/*
 * Interrupt Destination Addr Register Access -- Read/Write	    0000_0038
 */

void
pcireg_intr_dst_set(pcibr_soft_t ptr, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_wid_int = val;
}

/*
 * Intr Destination Addr Reg Access (target_id) -- Read/Write	    0000_0038
 */
uint64_t
pcireg_intr_dst_target_id_get(pcibr_soft_t ptr)
{
	uint64_t tid_bits;
	pic_t *bridge = (pic_t *)ptr->bs_base;

	tid_bits = (bridge->p_wid_int & PIC_INTR_DEST_TID);
	return (tid_bits >> PIC_INTR_DEST_TID_SHFT);
}

void
pcireg_intr_dst_target_id_set(pcibr_soft_t ptr, uint64_t target_id)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_wid_int &= ~PIC_INTR_DEST_TID;
	bridge->p_wid_int |=
		    ((target_id << PIC_INTR_DEST_TID_SHFT) & PIC_INTR_DEST_TID);
}

/*
 * Intr Destination Addr Register Access (addr) -- Read/Write	    0000_0038
 */
uint64_t
pcireg_intr_dst_addr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_wid_int & PIC_XTALK_ADDR_MASK;
}

void
pcireg_intr_dst_addr_set(pcibr_soft_t ptr, uint64_t addr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_wid_int &= ~PIC_XTALK_ADDR_MASK;
	bridge->p_wid_int |= (addr & PIC_XTALK_ADDR_MASK);
}

/*
 * Cmd Word Holding Bus Side Error Register Access -- Read Only	    0000_0040
 */
uint64_t
pcireg_cmdword_err_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_wid_err_cmdword;
}

/*
 * PCI/PCIX Target Flush Register Access -- Read Only		    0000_0050
 */
uint64_t
pcireg_tflush_get(pcibr_soft_t ptr)
{
	uint64_t ret = 0;
	pic_t *bridge = (pic_t *)ptr->bs_base;

	ret = bridge->p_wid_tflush;

	/* Read of the Targer Flush should always return zero */
	ASSERT_ALWAYS(ret == 0);
	return ret;
}

/*
 * Cmd Word Holding Link Side Error Register Access -- Read Only    0000_0058
 */
uint64_t
pcireg_linkside_err_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_wid_aux_err;
}

/*
 * PCI Response Buffer Address Holding Register -- Read Only	    0000_0068
 */
uint64_t
pcireg_resp_err_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_wid_resp;
}

/*
 * PCI Resp Buffer Address Holding Reg (Address) -- Read Only	    0000_0068
 */
uint64_t
pcireg_resp_err_addr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_wid_resp & PIC_RSP_BUF_ADDR;
}

/*
 * PCI Resp Buffer Address Holding Register (Buffer)-- Read Only    0000_0068
 */
uint64_t
pcireg_resp_err_buf_get(pcibr_soft_t ptr)
{
	uint64_t bufnum_bits;
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bufnum_bits = (bridge->p_wid_resp_upper & PIC_RSP_BUF_NUM);
	return (bufnum_bits >> PIC_RSP_BUF_NUM_SHFT);
}

/*
 * PCI Resp Buffer Address Holding Register (Device)-- Read Only    0000_0068
 */
uint64_t
pcireg_resp_err_dev_get(pcibr_soft_t ptr)
{
	uint64_t devnum_bits;
	pic_t *bridge = (pic_t *)ptr->bs_base;

	devnum_bits = (bridge->p_wid_resp_upper & PIC_RSP_BUF_DEV_NUM);
	return (devnum_bits >> PIC_RSP_BUF_DEV_NUM_SHFT);
}

/*
 * Address Holding Register Link Side Errors -- Read Only	    0000_0078
 */
uint64_t
pcireg_linkside_err_addr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_wid_addr_lkerr;
}

void
pcireg_dirmap_wid_set(pcibr_soft_t ptr, uint64_t target)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_dir_map &= ~PIC_DIRMAP_WID;
	bridge->p_dir_map |=
		    ((target << PIC_DIRMAP_WID_SHFT) & PIC_DIRMAP_WID);
}

void
pcireg_dirmap_diroff_set(pcibr_soft_t ptr, uint64_t dir_off)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_dir_map &= ~PIC_DIRMAP_DIROFF;
	bridge->p_dir_map |= (dir_off & PIC_DIRMAP_DIROFF);
}

void
pcireg_dirmap_add512_set(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_dir_map |= PIC_DIRMAP_ADD512;
}

void
pcireg_dirmap_add512_clr(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_dir_map &= ~PIC_DIRMAP_ADD512;
}

/*
 * PCI Page Map Fault Address Register Access -- Read Only	    0000_0090
 */
uint64_t
pcireg_map_fault_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_map_fault;
}

/*
 * Arbitration Register Access -- Read/Write			    0000_00A0
 */
uint64_t
pcireg_arbitration_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_arb;
}

void
pcireg_arbitration_bit_set(pcibr_soft_t ptr, uint64_t bits)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_arb |= bits;
}

/*
 * Internal Ram Parity Error Register Access -- Read Only	    0000_00B0
 */
uint64_t
pcireg_parity_err_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_ate_parity_err;
}

/*
 * Type 1 Configuration Register Access -- Read/Write		    0000_00C8
 */
void
pcireg_type1_cntr_set(pcibr_soft_t ptr, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_pci_cfg = val;
}

/*
 * PCI Bus Error Lower Addr Holding Reg Access -- Read Only	    0000_00D8
 */
uint64_t
pcireg_pci_bus_addr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_pci_err;
}

/*
 * PCI Bus Error Addr Holding Reg Access (Address) -- Read Only	    0000_00D8
 */
uint64_t
pcireg_pci_bus_addr_addr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_pci_err & PIC_XTALK_ADDR_MASK;
}

/*
 * Interrupt Status Register Access -- Read Only		    0000_0100
 */
uint64_t
pcireg_intr_status_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_int_status;
}

/*
 * Interrupt Enable Register Access -- Read/Write		    0000_0108
 */
uint64_t
pcireg_intr_enable_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_int_enable;
}

void
pcireg_intr_enable_set(pcibr_soft_t ptr, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_int_enable = val;
}

void
pcireg_intr_enable_bit_clr(pcibr_soft_t ptr, uint64_t bits)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_int_enable &= ~bits;
}

void
pcireg_intr_enable_bit_set(pcibr_soft_t ptr, uint64_t bits)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_int_enable |= bits;
}

/*
 * Interrupt Reset Register Access -- Write Only		    0000_0110
 */
void
pcireg_intr_reset_set(pcibr_soft_t ptr, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_int_rst_stat = val;
}

void
pcireg_intr_mode_set(pcibr_soft_t ptr, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_int_mode = val;
}

void
pcireg_intr_device_set(pcibr_soft_t ptr, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_int_device = val;
}

static void
__pcireg_intr_device_bit_set(pic_t *bridge, uint64_t bits)
{
	bridge->p_int_device |= bits;
}

void
pcireg_bridge_intr_device_bit_set(void *ptr, uint64_t bits)
{
	__pcireg_intr_device_bit_set((pic_t *)ptr, bits);
}

void
pcireg_intr_device_bit_set(pcibr_soft_t ptr, uint64_t bits)
{
	__pcireg_intr_device_bit_set((pic_t *)ptr->bs_base, bits);
}

void
pcireg_intr_device_bit_clr(pcibr_soft_t ptr, uint64_t bits)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_int_device &= ~bits;
}

/*
 * Host Error Interrupt Field Register Access -- Read/Write	    0000_0128
 */
void
pcireg_intr_host_err_set(pcibr_soft_t ptr, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_int_host_err = val;
}

/*
 * Interrupt Host Address Register -- Read/Write	0000_0130 - 0000_0168
 */
uint64_t
pcireg_intr_addr_get(pcibr_soft_t ptr, int int_n)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_int_addr[int_n];
}

static void
__pcireg_intr_addr_set(pic_t *bridge, int int_n, uint64_t val)
{
	bridge->p_int_addr[int_n] = val;
}

void
pcireg_bridge_intr_addr_set(void *ptr, int int_n, uint64_t val)
{
	__pcireg_intr_addr_set((pic_t *)ptr, int_n, val);
}

void
pcireg_intr_addr_set(pcibr_soft_t ptr, int int_n, uint64_t val)
{
	__pcireg_intr_addr_set((pic_t *)ptr->bs_base, int_n, val);
}

void *
pcireg_intr_addr_addr(pcibr_soft_t ptr, int int_n)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return (void *)&(bridge->p_int_addr[int_n]);
}

static void
__pcireg_intr_addr_vect_set(pic_t *bridge, int int_n, uint64_t vect)
{
	bridge->p_int_addr[int_n] &= ~PIC_HOST_INTR_FLD;
	bridge->p_int_addr[int_n] |=
		    ((vect << PIC_HOST_INTR_FLD_SHFT) & PIC_HOST_INTR_FLD);
}

void
pcireg_bridge_intr_addr_vect_set(void *ptr, int int_n, uint64_t vect)
{
	__pcireg_intr_addr_vect_set((pic_t *)ptr, int_n, vect);
}

void
pcireg_intr_addr_vect_set(pcibr_soft_t ptr, int int_n, uint64_t vect)
{
	__pcireg_intr_addr_vect_set((pic_t *)ptr->bs_base, int_n, vect);
}



/*
 * Intr Host Address Register (int_addr) -- Read/Write	0000_0130 - 0000_0168
 */
static void
__pcireg_intr_addr_addr_set(pic_t *bridge, int int_n, uint64_t addr)
{
	bridge->p_int_addr[int_n] &= ~PIC_HOST_INTR_ADDR;
	bridge->p_int_addr[int_n] |= (addr & PIC_HOST_INTR_ADDR);
}

void
pcireg_bridge_intr_addr_addr_set(void *ptr, int int_n, uint64_t addr)
{
	__pcireg_intr_addr_addr_set((pic_t *)ptr, int_n, addr);
}

void
pcireg_intr_addr_addr_set(pcibr_soft_t ptr, int int_n, uint64_t addr)
{
	__pcireg_intr_addr_addr_set((pic_t *)ptr->bs_base, int_n, addr);
}

/*
 * Multiple Interrupt Register Access -- Read Only		    0000_0178
 */
uint64_t
pcireg_intr_multiple_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_mult_int;
}

/*
 * Force Always Intr Register Access -- Write Only	0000_0180 - 0000_01B8
 */
static void *
__pcireg_force_always_addr_get(pic_t *bridge, int int_n)
{
	return (void *)&(bridge->p_force_always[int_n]);
}

void *
pcireg_bridge_force_always_addr_get(void *ptr, int int_n)
{
	return __pcireg_force_always_addr_get((pic_t *)ptr, int_n);
}

void *
pcireg_force_always_addr_get(pcibr_soft_t ptr, int int_n)
{
	return __pcireg_force_always_addr_get((pic_t *)ptr->bs_base, int_n);
}

/*
 * Force Interrupt Register Access -- Write Only	0000_01C0 - 0000_01F8
 */
void
pcireg_force_intr_set(pcibr_soft_t ptr, int int_n)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_force_pin[int_n] = 1;
}

/*
 * Device(x) Register Access -- Read/Write		0000_0200 - 0000_0218
 */
uint64_t
pcireg_device_get(pcibr_soft_t ptr, int device)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	ASSERT_ALWAYS((device >= 0) && (device <= 3));
	return bridge->p_device[device];
}

void
pcireg_device_set(pcibr_soft_t ptr, int device, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	ASSERT_ALWAYS((device >= 0) && (device <= 3));
	bridge->p_device[device] = val;
}

/*
 * Device(x) Write Buffer Flush Reg Access -- Read Only	0000_0240 - 0000_0258
 */
uint64_t
pcireg_wrb_flush_get(pcibr_soft_t ptr, int device)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;
	uint64_t ret = 0;

	ASSERT_ALWAYS((device >= 0) && (device <= 3));
	ret = bridge->p_wr_req_buf[device];

	/* Read of the Write Buffer Flush should always return zero */
	ASSERT_ALWAYS(ret == 0);
	return ret;
}

/*
 * Even/Odd RRB Register Access -- Read/Write		0000_0280 - 0000_0288
 */
uint64_t
pcireg_rrb_get(pcibr_soft_t ptr, int even_odd)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_rrb_map[even_odd];
}

void
pcireg_rrb_set(pcibr_soft_t ptr, int even_odd, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_rrb_map[even_odd] = val;
}

void
pcireg_rrb_bit_set(pcibr_soft_t ptr, int even_odd, uint64_t bits)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_rrb_map[even_odd] |= bits;
}

/*
 * RRB Status Register Access -- Read Only			    0000_0290
 */
uint64_t
pcireg_rrb_status_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_resp_status;
}

/*
 * RRB Clear Register Access -- Write Only			    0000_0298
 */
void
pcireg_rrb_clear_set(pcibr_soft_t ptr, uint64_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	bridge->p_resp_clear = val;
}

/*
 * PCIX Bus Error Address Register Access -- Read Only		    0000_0600
 */
uint64_t
pcireg_pcix_bus_err_addr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_pcix_bus_err_addr;
}

/*
 * PCIX Bus Error Attribute Register Access -- Read Only	    0000_0608
 */
uint64_t
pcireg_pcix_bus_err_attr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_pcix_bus_err_attr;
}

/*
 * PCIX Bus Error Data Register Access -- Read Only		    0000_0610
 */
uint64_t
pcireg_pcix_bus_err_data_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_pcix_bus_err_data;
}

/*
 * PCIX PIO Split Request Address Register Access -- Read Only	    0000_0618
 */
uint64_t
pcireg_pcix_pio_split_addr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_pcix_pio_split_addr;
}

/*
 * PCIX PIO Split Request Attribute Register Access -- Read Only    0000_0620
 */
uint64_t
pcireg_pcix_pio_split_attr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_pcix_pio_split_attr;
}

/*
 * PCIX DMA Request Error Attribute Register Access -- Read Only    0000_0628
 */
uint64_t
pcireg_pcix_req_err_attr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_pcix_dma_req_err_attr;
}

/*
 * PCIX DMA Request Error Address Register Access -- Read Only	    0000_0630
 */
uint64_t
pcireg_pcix_req_err_addr_get(pcibr_soft_t ptr)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	return bridge->p_pcix_dma_req_err_addr;
}

/*
 * Type 0 Configuration Space Access -- Read/Write
 */
cfg_p
pcireg_type0_cfg_addr(pcibr_soft_t ptr, uint8_t slot, uint8_t func, int off)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	/* Type 0 Config space accesses on PIC are 1-4, not 0-3 since
	 * it is a PCIX Bridge.  See sys/PCI/pic.h for explanation.
	 */
	slot++;
	ASSERT_ALWAYS(((int) slot >= 1) && ((int) slot <= 4));
	return &(bridge->p_type0_cfg_dev[slot].f[func].l[(off / 4)]);
}

/*
 * Type 1 Configuration Space Access -- Read/Write
 */
cfg_p
pcireg_type1_cfg_addr(pcibr_soft_t ptr, uint8_t func, int offset)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	/*
	 * Return a config space address for the given slot/func/offset.
	 * Note the returned ptr is a 32bit word (ie. cfg_p) aligned ptr
	 * pointing to the 32bit word that contains the "offset" byte.
	 */
	return &(bridge->p_type1_cfg.f[func].l[(offset / 4)]);
}

/*
 * Internal ATE SSRAM Access -- Read/Write 
 */
bridge_ate_t
pcireg_int_ate_get(pcibr_soft_t ptr, int ate_index)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	ASSERT_ALWAYS((ate_index >= 0) && (ate_index <= 1024));
	return bridge->p_int_ate_ram[ate_index];
}

void
pcireg_int_ate_set(pcibr_soft_t ptr, int ate_index, bridge_ate_t val)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	ASSERT_ALWAYS((ate_index >= 0) && (ate_index <= 1024));
	bridge->p_int_ate_ram[ate_index] = (picate_t) val;
}

bridge_ate_p
pcireg_int_ate_addr(pcibr_soft_t ptr, int ate_index)
{
	pic_t *bridge = (pic_t *)ptr->bs_base;

	ASSERT_ALWAYS((ate_index >= 0) && (ate_index <= 1024));
	return &(bridge->p_int_ate_ram[ate_index]);
}

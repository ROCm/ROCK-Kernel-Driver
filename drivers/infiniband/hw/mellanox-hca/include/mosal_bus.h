/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef H_MOSAL_BUS_H
#define H_MOSAL_BUS_H

#include <mtl_types.h>
#include <mtl_pci_types.h>

typedef struct pci_dev *MOSAL_pci_dev_t;

/******************************************************************************
 *  Function: MOSAL_PCI_present
 *
 *  Description: Check on existance of PCI in system.
 *    This function should be called before using any of the following.
 *
 *  Parameters: (none)
 *
 *  Returns:
 *    bool
 *        TRUE if PCI exist.
 *
 ******************************************************************************/
bool MOSAL_PCI_present(void);



/******************************************************************************
 *  Function: MOSAL_PCI_find_dev
 *
 *  Description: Find a PCI device based on Vendor and deviceID.
 *
 *  Parameters:
 *    vendor_id(IN)   u_int16_t
 *         Vendor ID.
 *    dev_id(IN)      u_int16_t
 *         Device ID.
 *    prev_dev(IN)  
 *         previous device or NULL if first
 *    new_dev_p(OUT)  
 *         pointer to found device
 *    bus_p(OUT)      u_int8_t *
 *         Bus num of matching device.
 *    dev_func_p(OUT) u_int8_t *
 *         Device/Function ([7:3]/[2:0]) of matching device.
 *
 *  Returns:
 *    call_result_t
 *        MT_OK if found, MT_ENRSC if such device not found.
 *
 *  Note:
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_find_dev(u_int16_t vendor_id,
                                 u_int16_t dev_id,
                                 MOSAL_pci_dev_t prev_dev,
                                 MOSAL_pci_dev_t *new_dev_p,
                                 u_int8_t *bus_p,
                                 u_int8_t *dev_func_p);

/******************************************************************************
 *  Function: MOSAL_PCI_find_device
 *
 *  Description: Find a PCI device based on Vendor and deviceID.
 *
 *  Parameters:
 *    vendor_id(IN)   u_int16_t
 *         Vendor ID.
 *    dev_id(IN)      u_int16_t
 *         Device ID.
 *    index(IN)       u_int16_t
 *         Occurance of device of given Vendor/Device IDs.
 *    bus_p(OUT)      u_int8_t *
 *         Bus num of matching device.
 *    dev_func_p(OUT) u_int8_t *
 *         Device/Function ([7:3]/[2:0]) of matching device.
 *
 *  Returns:
 *    call_result_t
 *        MT_OK if found, MT_ENODEV if such device not found.
 *
 *  Note:
 *  For hot-swap support, the PCI bus should be really probed on device
 *  search, and not a preset DB (which was usually created during boot).
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_find_device(u_int16_t vendor_id, u_int16_t dev_id,
                                    u_int16_t index,
                                    u_int8_t *bus_p, u_int8_t *dev_func_p);


/******************************************************************************
 *  Function: MOSAL_PCI_find_class
 *
 *  Description: Find a PCI device based on class code.
 *
 *  Parameters:
 *    class_code(IN)  u_int32_t
 *         24 Class code bits.
 *    index(IN)       u_int16_t
 *         Occurance of device of given Vendor/Device IDs.
 *    bus_p(OUT)      u_int8_t *
 *         Bus num of matching device.
 *    dev_func_p(OUT) u_int8_t *
 *         Device/Function ([7:3]/[2:0]) of matching device.
 *
 *  Returns:
 *    call_result_t
 *        MT_OK if found, MT_ENODEV if such device not found.
 *
 *  Note:
 *  For hot-swap support, the PCI bus should be really probed on device
 *  search, and not a preset DB (which was usually created during boot).
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_find_class(u_int32_t class_code, u_int16_t index,
                                   u_int8_t *bus_p, u_int8_t *dev_func_p);



/******************************************************************************
 *  Function: MOSAL_PCI_read_config
 *
 *  Description: read from configuration space
 *
 *  Parameters:
 *    pci_dev(in) pointer to pci device
 *    offset(in) offset in configuration space
 *    size(in) number of bytes to read 1,2 or 4
 *    data_p(out) pointer to returned data
 *
 *  Returns:
 *    call_result_t
 *        MT_OK
 *        MT_ERROR
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_read_config(MOSAL_pci_dev_t pci_dev,
                                    u_int8_t offset,
                                    u_int8_t size,
                                    void* data_p);


/******************************************************************************
 *  Function: MOSAL_PCI_write_config
 *
 *  Description: read from configuration space
 *
 *  Parameters:
 *    pci_dev(in) pointer to pci device
 *    offset(in) offset in configuration space
 *    size(in) number of bytes to read 1,2 or 4
 *    data(in) data to write
 *
 *  Returns:
 *    call_result_t
 *        MT_OK
 *        MT_ERROR
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_write_config(MOSAL_pci_dev_t pci_dev,
                                     u_int8_t offset,
                                     u_int8_t size,
                                     u_int32_t data);


/******************************************************************************
 *  Function: MOSAL_PCI_read_config_byte
 *
 *  Description: Read byte of PCI config space.
 *
 *  Parameters:
 *    bus(IN)       u_int8_t
 *         Bus num of device.
 *    dev_func(IN)  u_int8_t
 *         Device/Function ([7:3]/[2:0]) of device.
 *    offset(IN)    u_int8_t
 *         Offset in device's config header.
 *    data_p(OUT)   u_int8_t*
 *         Ptr to a byte data buffer which holds read val.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_read_config_byte(u_int8_t bus, u_int8_t dev_func,
                                         u_int8_t offset, u_int8_t* data_p);



/******************************************************************************
 *  Function: MOSAL_PCI_read_config_word
 *
 *  Description: Read byte of PCI config space.
 *
 *  Parameters:
 *    bus(IN)       u_int8_t
 *         Bus num of device.
 *    dev_func(IN)  u_int8_t
 *         Device/Function ([7:3]/[2:0]) of device.
 *    offset(IN)    u_int8_t
 *         Offset in device's config header.
 *    data_p(OUT)   u_int16_t*
 *         Ptr to a word data buffer which holds read val.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_read_config_word(u_int8_t bus, u_int8_t dev_func,
                                         u_int8_t offset, u_int16_t* data_p);



/******************************************************************************
 *  Function: MOSAL_PCI_read_config_dword
 *
 *  Description: Read byte of PCI config space.
 *
 *  Parameters:
 *    bus(IN)       u_int8_t
 *         Bus num of device.
 *    dev_func(IN)  u_int8_t
 *         Device/Function ([7:3]/[2:0]) of device.
 *    offset(IN)    u_int8_t
 *         Offset in device's config header.
 *    data_p(OUT)   u_int32_t*
 *         Ptr to a dword data buffer which holds read val.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_read_config_dword(u_int8_t bus, u_int8_t dev_func,
                                         u_int8_t offset, u_int32_t* data_p);



/******************************************************************************
 *  Function: MOSAL_PCI_write_config_byte
 *
 *  Description: Write byte to PCI config space.
 *
 *  Parameters:
 *    bus(IN)       u_int8_t
 *         Bus num of device.
 *    dev_func(IN)  u_int8_t
 *         Device/Function ([7:3]/[2:0]) of device.
 *    offset(IN)    u_int8_t
 *         Offset in device's config header.
 *    data(IN)     u_int8_t
 *         Val to write.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_write_config_byte(u_int8_t bus, u_int8_t dev_func,
                                          u_int8_t offset, u_int8_t data);



/******************************************************************************
 *  Function: MOSAL_PCI_write_config_word
 *
 *  Description: Write word of PCI config space.
 *
 *  Parameters:
 *    bus(IN)       u_int8_t
 *         Bus num of device.
 *    dev_func(IN)  u_int8_t
 *         Device/Function ([7:3]/[2:0]) of device.
 *    offset(IN)    u_int8_t
 *         Offset in device's config header.
 *    data(IN)      u_int16_t
 *         Val to write.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_write_config_word(u_int8_t bus, u_int8_t dev_func,
                                          u_int8_t offset, u_int16_t data);



/******************************************************************************
 *  Function: MOSAL_PCI_write_config_dword
 *
 *  Description: Write dword of PCI config space.
 *
 *  Parameters:
 *    bus(IN)       u_int8_t
 *         Bus num of device.
 *    dev_func(IN)  u_int8_t
 *         Device/Function ([7:3]/[2:0]) of device.
 *    offset(IN)    u_int8_t
 *         Offset in device's config header.
 *    data(IN)      u_int32_t
 *         Val to write.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_write_config_dword(u_int8_t bus,
                                           u_int8_t dev_func,
                                           u_int8_t offset,
                                           u_int32_t data);

/******************************************************************************
 *  Function: MOSAL_PCI_get_cfg_hdr
 *
 *  Description: Get header type0 or type1
 *
 *  Parameters:
 *    pci_dev(IN)   pointer pci device 
 *    
 *    cfg_hdr(OUT)   MOSAL_PCI_cfg_hdr_t
 *         Union of type0 and type1 header.
 *    
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR
 *
 *  Notes:
 *    If header type is unknown it can be extracted from header_type field of 
 *    type0 member offset or type1. 
 *     
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_get_cfg_hdr(MOSAL_pci_dev_t pci_dev,  MOSAL_PCI_cfg_hdr_t * cfg_hdr);





/******************************************************************************
 *  Function: MOSAL_PCI_read_io_byte
 *
 *  Description: Read byte of PCI I/O space.
 *
 *  Parameters:
 *    addr(IN)      u_int32_t
 *         I/O address to read.
 *    data_p(OUT)    u_int8_t *
 *         Ptr to a byte data buffer.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_read_io_byte(u_int32_t addr, u_int8_t *data_p);



/******************************************************************************
 *  Function: MOSAL_PCI_read_io_word
 *
 *  Description: Read word of PCI I/O space.
 *
 *  Parameters:
 *    addr(IN)      u_int32_t
 *         I/O address to read.
 *    data_p(OUT)    u_int16_t *
 *         Ptr to a word data buffer.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_read_io_word(u_int32_t addr, u_int16_t *data_p);



/******************************************************************************
 *  Function: MOSAL_PCI_read_io_dword
 *
 *  Description: Read dword of PCI I/O space.
 *
 *  Parameters:
 *    addr(IN)      u_int32_t
 *         I/O address to read.
 *    data_p(OUT)    u_int32_t *
 *         Ptr to a dword data buffer.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_read_io_dword(u_int32_t addr, u_int32_t *data_p);



/******************************************************************************
 *  Function: MOSAL_PCI_write_io_byte
 *
 *  Description: Write byte of PCI I/O space.
 *
 *  Parameters:
 *    addr(IN)    u_int32_t
 *         I/O address to write.
 *    data(IN)    u_int8_t
 *         Byte data to write.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_write_io_byte(u_int32_t addr, u_int8_t data);



/******************************************************************************
 *  Function: MOSAL_PCI_write_io_word
 *
 *  Description: Write word of PCI I/O space.
 *
 *  Parameters:
 *    addr(IN)    u_int32_t
 *         I/O address to write.
 *    data(IN)    u_int16_t
 *         Word data to write.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_write_io_word(u_int32_t addr, u_int16_t data);



/******************************************************************************
 *  Function: MOSAL_PCI_write_io_dword
 *
 *  Description: Write dword of PCI I/O space.
 *
 *  Parameters:
 *    addr(IN)    u_int32_t
 *         I/O address to write.
 *    data(IN)    u_int32_t
 *         Dword data to write.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_PCI_write_io_dword(u_int32_t addr, u_int32_t data);



/**************************************************************************************************
 *                                        MPC860 bus
 **************************************************************************************************/


/******************************************************************************
 *  Function: MOSAL_MPC860_present
 *
 *  Description: Check on existance of MPC860 bus in system.
 *    This function should be called before using any of the following.
 *
 *  Parameters: (none)
 *
 *  Returns:
 *    bool
 *         TRUE if MPC860 bus exist.
 *
 ******************************************************************************/
bool MOSAL_MPC860_present(void);

/******************************************************************************
 *  Function: MOSAL_MPC860_read
 *
 *  Description: Read MPC860 External Bus mem. space.
 *
 *  Parameters:
 *    addr(IN)      u_int32_t
 *         Address to read.
 *    size(IN)      u_int32_t
 *         Num. of bytes to read.
 *    data_p(OUT) (LEN @size)   void *
 *         Ptr to data buffer of 'size' bytes at least.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_MPC860_read(u_int32_t addr, u_int32_t size, void *data_p);

/******************************************************************************
 *  Function: MOSAL_MPC860_write
 *
 *  Description: Write MPC860 External Bus mem. space.
 *
 *  Parameters:
 *    addr(IN)      u_int32_t
 *         Address to write.
 *    size(IN)      u_int32_t
 *         Num. of bytes to write.
 *    data_p(IN) (LEN @size)   void *
 *         Ptr to data buffer of 'size' bytes at least.
 *
 *  Returns:
 *    call_result_t
 *         MT_OK if success, MT_ERROR if failed.
 *
 ******************************************************************************/
call_result_t MOSAL_MPC860_write(u_int32_t addr, u_int32_t size, void *data_p);


#endif /* H_MOSAL_BUS_H */

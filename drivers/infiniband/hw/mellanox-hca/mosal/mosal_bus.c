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

//#include "mdhal.h"
//#include "mdoal.h"
#include "mosal_priv.h"

 
/*
 * PCI
 * ---
 */
 
#if LINUX_KERNEL_2_6
/* OK, this is a hacked-up bandaid.  The real fix is for the whole
   driver stack to use opaque PCI device handles instead of dealing in
   terms of bus/device/function numbers.  But by putting this kludge
   here I get away with leaving the rest of the code alone. */

static int buses_inited = 0;
static struct pci_bus *bus_table[256];

static void MOSAL_PCI_init_bus_table(void)
{
  int i;

  for ( i=0; i<256; ++i ) {
    bus_table[i] = pci_find_bus(0, i);
  }
}

static struct pci_bus *MOSAL_PCI_get_bus_by_number(u_int8_t bus)
{
  if ( !buses_inited ) {
    MOSAL_PCI_init_bus_table();
    buses_inited = 1;
  }

  return bus_table[bus];
}
#endif
 
 
 
bool MOSAL_PCI_present(void)
{
  bool rc;
  MTL_TRACE2("MOSAL_PCI_present\n");
#ifdef CONFIG_PCI
#if LINUX_KERNEL_2_6
  rc = TRUE;
#else
  rc = pcibios_present();
#endif
#else
  rc = FALSE;
#endif

  MTL_TRACE1("<- MOSAL_PCI_present rc=%s, \n",rc ? "TRUE":"FALSE");
  return rc;
}


call_result_t MOSAL_PCI_find_dev(u_int16_t vendor_id,
                                 u_int16_t dev_id,
                                 MOSAL_pci_dev_t prev_dev,
                                 MOSAL_pci_dev_t *new_dev_p,
                                 u_int8_t * bus_p,
                                 u_int8_t * dev_func_p)
{
  MOSAL_pci_dev_t new_dev;

  new_dev = pci_find_device(vendor_id, dev_id, prev_dev);
  if ( new_dev ) {
    *bus_p = new_dev->bus->number;
    *dev_func_p = new_dev->devfn;
    *new_dev_p = new_dev;
    return MT_OK;
  }
  else {
    return MT_ENORSC;
  }
}


call_result_t MOSAL_PCI_find_device(u_int16_t vendor_id,
                                    u_int16_t dev_id,
                                    u_int16_t index,
                                    u_int8_t * bus_p,
                                    u_int8_t * dev_func_p)
{
    call_result_t rc;

    MTL_TRACE2("<- MOSAL_PCI_find_device; vendor_id=0x%x, dev_id=0x%x, index=0x%x\n",
               vendor_id, dev_id, index);
#ifdef CONFIG_PCI
#if LINUX_KERNEL_2_6
    {
      u_int16_t i;
      struct pci_dev *dev = NULL;

      rc = PCIBIOS_SUCCESSFUL;
      for ( i=0; i<index; ++i ) {
        dev = pci_get_device(vendor_id, dev_id, dev);
        if ( dev == NULL ) {
          rc = PCIBIOS_DEVICE_NOT_FOUND;
          break;
        }
      }
      if ( rc == PCIBIOS_SUCCESSFUL ) {
        dev = pci_get_device(vendor_id, dev_id, dev);
        if ( dev ) {
          *bus_p = dev->bus->number;
          *dev_func_p = dev->devfn;
        }
        else {
          rc = PCIBIOS_DEVICE_NOT_FOUND;
        }
      }
    }
#else
    rc = pcibios_find_device(vendor_id, dev_id, index, bus_p, dev_func_p);
#endif
    switch ( rc ) {
      case PCIBIOS_DEVICE_NOT_FOUND:
        rc = MT_ENORSC;
        break;
      case PCIBIOS_SUCCESSFUL:
        rc = MT_OK;
        break;
      default:
        rc = MT_ERROR;
    }
#else
    rc = MT_ENORSC;
#endif
    MTL_TRACE2("-> MOSAL_PCI_find_device; rc=%d, bus=0x%x, dev_func=0x%x\n",
               rc, *bus_p, *dev_func_p);
    return rc;
}

call_result_t MOSAL_PCI_find_class(u_int32_t class_code,
                                   u_int16_t index,
                                   u_int8_t * bus_p,
                                   u_int8_t * dev_func_p)
{
    call_result_t rc;

    MTL_TRACE2("<- MOSAL_PCI_find_class; class_code=0x%x, index=0x%x\n",
               class_code, index);
#ifdef CONFIG_PCI
#if LINUX_KERNEL_2_6
    {
      u_int16_t i;
      struct pci_dev *dev = NULL;

      rc = MT_OK;
      for ( i=0; i<index; ++i ) {
        dev = pci_find_class(class_code, dev);
        if ( dev == NULL ) {
          rc = MT_ENORSC;
          break;
        }
      }
      if ( rc == MT_OK ) {
        dev = pci_find_class(class_code, dev);
        if ( dev ) {
          *bus_p = dev->bus->number;
          *dev_func_p = dev->devfn;
        }
        else {
          rc = MT_ENORSC;
        }
      }
    }
#else
    rc = pcibios_find_class(class_code, index, bus_p, dev_func_p);
#endif
#else
    rc = MT_ENORSC;
#endif
    MTL_TRACE2("-> MOSAL_PCI_find_class; rc=%d, bus=0x%x, dev_func=0x%x\n",
               rc, *bus_p, *dev_func_p);
    return rc;
}


call_result_t MOSAL_PCI_read_config(MOSAL_pci_dev_t pci_dev,
                                    u_int8_t offset,
                                    u_int8_t size,
                                    void* data_p)
{
#ifdef CONFIG_PCI
	struct pci_bus	*bus;		/* bus this device is on */
  int ret;

  if ( !pci_dev ) return MT_EINVAL;
  bus = pci_dev->bus;
  if ( !bus ) return MT_EINVAL;

  if ( size == 1 ) {
#if LINUX_KERNEL_2_6
    ret= pci_bus_read_config_byte(bus, pci_dev->devfn, offset, data_p);
#else
    ret = pcibios_read_config_byte(bus->number, pci_dev->devfn, offset, data_p);
#endif
  }
  else if ( size == 2 ) {
#if LINUX_KERNEL_2_6
    ret= pci_bus_read_config_word(bus, pci_dev->devfn, offset, (u16 *)data_p);
#else
    ret = pcibios_read_config_word(bus->number, pci_dev->devfn, offset, data_p);
#endif
  }
  else if ( size == 4 ) {
#if LINUX_KERNEL_2_6
    ret= pci_bus_read_config_dword(bus, pci_dev->devfn, offset, (u32 *)data_p);
#else
    ret = pcibios_read_config_dword(bus->number, pci_dev->devfn, offset, data_p);
#endif
  }
  else ret = -1;

  if ( ret != 0 ) {
    return MT_ERROR;
  }
  else {
    return MT_OK;
  }
#else
  return MT_ENOSYS;
#endif
}


call_result_t MOSAL_PCI_write_config(MOSAL_pci_dev_t pci_dev,
                                     u_int8_t offset,
                                     u_int8_t size,
                                     u_int32_t data)
{
#ifdef CONFIG_PCI
	struct pci_bus	*bus;		/* bus this device is on */
  int ret;

  if ( !pci_dev ) return MT_EINVAL;
  bus = pci_dev->bus;
  if ( !bus ) return MT_EINVAL;

  if ( size == 1 ) {
#if LINUX_KERNEL_2_6
    ret= pci_bus_write_config_byte(bus, pci_dev->devfn, offset, data);
#else
    ret = pcibios_write_config_byte(bus->number, pci_dev->devfn, offset, data);
#endif
  }
  else if ( size == 2 ) {
#if LINUX_KERNEL_2_6
    ret= pci_bus_write_config_word(bus, pci_dev->devfn, offset, data);
#else
    ret = pcibios_write_config_word(bus->number, pci_dev->devfn, offset, data);
#endif
  }
  else if ( size == 4 ) {
#if LINUX_KERNEL_2_6
    ret= pci_bus_write_config_dword(bus, pci_dev->devfn, offset, data);
#else
    ret = pcibios_write_config_dword(bus->number, pci_dev->devfn, offset, data);
#endif
  }
  else ret = -1;

  if ( ret != 0 ) {
    return MT_ERROR;
  }
  else {
    return MT_OK;
  }
#else
  return MT_ENOSYS;
#endif
}


call_result_t MOSAL_PCI_read_config_byte(u_int8_t bus,
                                         u_int8_t dev_func,
                                         u_int8_t offset,
                                         u_int8_t* data_p)
{
    call_result_t rc;

    MTL_TRACE2("<- MOSAL_PCI_read_config_byte; bus=0x%x, dev_func=0x%x, offset=0x%x\n",
               bus, dev_func, offset);
#ifdef CONFIG_PCI
#if LINUX_KERNEL_2_6
    {
      struct pci_bus *p_bus;

      p_bus = MOSAL_PCI_get_bus_by_number(bus);
      if ( p_bus ) {
        rc = pci_bus_read_config_byte(p_bus, dev_func, offset, data_p);
      }
      else rc = MT_ENODEV;
    }
#else
    rc = pcibios_read_config_byte(bus, dev_func, offset, data_p);
#endif
#else
    rc = MT_ENORSC;
#endif
    MTL_TRACE2("-> MOSAL_PCI_read_config_byte; rc=%d, data=0x%x\n", rc, *
data_p);
    return rc;
}

call_result_t MOSAL_PCI_read_config_word(u_int8_t bus,
                                         u_int8_t dev_func,
                                         u_int8_t offset,
                                         u_int16_t* data_p)
{
    call_result_t rc;

    MTL_TRACE2("<- MOSAL_PCI_read_config_word; bus=0x%x, dev_func=0x%x, offset=0x%x\n",
               bus, dev_func, offset);
#ifdef CONFIG_PCI
#if LINUX_KERNEL_2_6
    rc = pci_bus_read_config_word(MOSAL_PCI_get_bus_by_number(bus), dev_func, offset, data_p);
#else
    rc = pcibios_read_config_word(bus, dev_func, offset, data_p);
#endif
#else
    rc = MT_ENORSC;
#endif
    MTL_TRACE2("-> MOSAL_PCI_read_config_word; rc=%d, data=0x%x\n", rc, *
data_p);
    return rc;
}

call_result_t MOSAL_PCI_read_config_dword(u_int8_t bus,
                                          u_int8_t dev_func,
                                          u_int8_t offset,
                                          u_int32_t* data_p)
{
    call_result_t rc;

    MTL_TRACE2("<- MOSAL_PCI_read_config_dword; bus=0x%x, dev_func=0x%x, offset=0x%x\n",
               bus, dev_func, offset);
#ifdef CONFIG_PCI
#if LINUX_KERNEL_2_6
    rc = pci_bus_read_config_dword(MOSAL_PCI_get_bus_by_number(bus), dev_func, offset, data_p);
#else
    rc = pcibios_read_config_dword(bus, dev_func, offset, data_p);
#endif
#else
    rc = MT_ENORSC;
#endif
    MTL_TRACE2("-> MOSAL_PCI_read_config_dword; rc=%d, data=0x%x\n", rc, *
data_p);
    return rc;
}

call_result_t MOSAL_PCI_write_config_byte(u_int8_t bus,
                                          u_int8_t dev_func,
                                          u_int8_t offset,
                                          u_int8_t data)
{
    call_result_t rc;

    MTL_TRACE2("<- MOSAL_PCI_write_config_byte; bus=0x%x, dev_func=0x%x, offset=0x%x, data=0x%x\n",
               bus, dev_func, offset, data);
#ifdef CONFIG_PCI
#if LINUX_KERNEL_2_6
    rc = pci_bus_write_config_byte(MOSAL_PCI_get_bus_by_number(bus), dev_func, offset, data);
#else
    rc = pcibios_write_config_byte(bus, dev_func, offset, data);
#endif
#else
    rc = MT_ENORSC;
#endif
    MTL_TRACE2("-> MOSAL_PCI_write_config_byte; rc=%d\n", rc);
    return rc;
}

call_result_t MOSAL_PCI_write_config_word(u_int8_t bus,
                                          u_int8_t dev_func,
                                          u_int8_t offset,
                                          u_int16_t data)
{
    call_result_t rc;

    MTL_TRACE2("<- MOSAL_PCI_write_config_word; bus=0x%x, dev_func=0x%x, offset=0x%x, data=0x%x\n",
               bus, dev_func, offset, data);
#ifdef CONFIG_PCI
#if LINUX_KERNEL_2_6
    rc = pci_bus_write_config_word(MOSAL_PCI_get_bus_by_number(bus), dev_func, offset, data);
#else
    rc = pcibios_write_config_word(bus, dev_func, offset, data);
#endif
#else
    rc = MT_ENORSC;
#endif
    MTL_TRACE2("-> MOSAL_PCI_write_config_word; rc=%d\n", rc);
    return rc;
}

call_result_t MOSAL_PCI_write_config_dword(u_int8_t bus,
                                           u_int8_t dev_func,
                                           u_int8_t offset,
                                           u_int32_t data)
{
    call_result_t rc;

    MTL_TRACE2("<- MOSAL_PCI_write_config_dword; bus=0x%x, dev_func=0x%x, offset=0x%x, data=0x%x\n",
               bus, dev_func, offset, data);
#ifdef CONFIG_PCI
#if LINUX_KERNEL_2_6
    rc = pci_bus_write_config_dword(MOSAL_PCI_get_bus_by_number(bus), dev_func, offset, data);
#else
    rc = pcibios_write_config_dword(bus, dev_func, offset, data);
#endif
#else
    rc = MT_ENORSC;
#endif
    MTL_TRACE2("-> MOSAL_PCI_write_config_dword; rc=%d\n", rc);
    return rc;
}

call_result_t MOSAL_PCI_read_io_byte(u_int32_t addr, u_int8_t *data_p)
{
    MTL_TRACE2("<- MOSAL_PCI_read_io_byte; addr=0x%x\n", addr);
#if defined(CONFIG_PCI) && (defined(__i386__) || defined(__ia64__))
    *data_p = inb(addr);
    MTL_TRACE2("-> MOSAL_PCI_read_io_byte; data=0x%x\n", *data_p);
    return MT_OK;
#else
    return MT_ENORSC;
#endif
}

call_result_t MOSAL_PCI_read_io_word(u_int32_t addr, u_int16_t *data_p)
{
    MTL_TRACE2("<- MOSAL_PCI_read_io_word; addr=0x%x\n", addr);
#if defined(CONFIG_PCI) && (defined(__i386__) || defined(__ia64__))
    *data_p = inw(addr);
    MTL_TRACE2("-> MOSAL_PCI_read_io_word; data=0x%x\n", *data_p);
    return MT_OK;
#else
    return MT_ENORSC;
#endif
}

call_result_t MOSAL_PCI_read_io_dword(u_int32_t addr, u_int32_t *data_p)
{
    MTL_TRACE2("<- MOSAL_PCI_read_io_dword; addr=0x%x\n", addr);
#if defined(CONFIG_PCI) && (defined(__i386__) || defined(__ia64__))
    *data_p = inl(addr);
    MTL_TRACE2("-> MOSAL_PCI_read_io_dword; data=0x%x\n", *data_p);
    return MT_OK;
#else
    return MT_ENORSC;
#endif
}

call_result_t MOSAL_PCI_write_io_byte(u_int32_t addr,
                                      u_int8_t data)
{
    MTL_TRACE2("MOSAL_PCI_write_io_byte; addr=0x%x, data=0x%x\n", addr, data);
#if defined(CONFIG_PCI) && (defined(__i386__) || defined(__ia64__))
    outb(addr, data);
    return MT_OK;
#else
    return MT_ENORSC;
#endif
}

call_result_t MOSAL_PCI_write_io_word(u_int32_t addr,
                                      u_int16_t data)
{
    MTL_TRACE2("MOSAL_PCI_write_io_word; addr=0x%x, data=0x%x\n", addr, data);
#if defined(CONFIG_PCI) && (defined(__i386__) || defined(__ia64__))
    outw(addr, data);
    return MT_OK;
#else
    return MT_ENORSC;
#endif
}

call_result_t MOSAL_PCI_write_io_dword(u_int32_t addr,
                                       u_int32_t data)
{
    MTL_TRACE2("MOSAL_PCI_write_io_dword; addr=0x%x, data=0x%x\n", addr, data);
#if defined(CONFIG_PCI) && (defined(__i386__) || defined(__ia64__))
    outl(addr, data);
    return MT_OK;
#else
    return MT_ENORSC;
#endif
}

/*
 * MPC860
 * ------
 */
bool MOSAL_MPC860_present()
{
    bool rc;

    MTL_TRACE1("\n-> MOSAL_MPC860_present \n");

#ifdef PPC_PRESENT
    rc = TRUE;
#else
    rc = FALSE;
#endif

    MTL_TRACE1("<- MOSAL_MPC860_present rc=%s, \n", rc ? "TRUE":"FALSE");
    return rc;
}


call_result_t MOSAL_MPC860_read(u_int32_t addr,
                                u_int32_t size,
                                void * data_p)
{
    call_result_t rc;

    MTL_TRACE1("\n-> MOSAL_MPC860_read addr=%08x, size=%d, data_p=%p\n",
               addr, size, data_p);

#ifdef PPC_PRESENT
    rc = MOSAL_MAPP_mem_read(addr, size, data_p);
#else
    rc = MT_ENOSYS;
#endif

    MTL_TRACE1("<- MOSAL_MPC860_read rc=%d (%s)\n",
               rc, mtl_strerror_sym(rc));
    return rc;
}

call_result_t MOSAL_MPC860_write(u_int32_t addr,
                                 u_int32_t size,
                                 void * data_p)
{
    call_result_t rc;

    MTL_TRACE1("\n-> MOSAL_MPC860_write addr=%08x, size=%d, data_p=%p\n",
               addr, size, data_p);

#ifdef PPC_PRESENT
    rc = MOSAL_MAPP_mem_write(addr, size, data_p);
#else
    rc = MT_ENOSYS;
#endif

    MTL_TRACE1("<- MOSAL_MPC860_write rc=%d (%s)\n",
               rc, mtl_strerror_sym(rc));
    return rc;
}


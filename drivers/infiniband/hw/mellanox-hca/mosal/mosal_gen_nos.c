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


#include <mtl_common.h>
#include <mosal.h>

#ifdef __DARWIN__
#error This function is unsupported in Darwin
#endif

call_result_t MOSAL_PCI_get_cfg_hdr(MOSAL_pci_dev_t pci_dev, MOSAL_PCI_cfg_hdr_t * cfg_hdr)
{
    call_result_t ret;

    u_int8_t  offset;
    u_int32_t dword;

    MOSAL_PCI_hdr_type0_t * t0 = 0;
    MOSAL_PCI_hdr_type1_t * t1 = 0;

    for (offset = 0; offset < sizeof(MOSAL_PCI_cfg_hdr_t); offset += sizeof(u_int32_t))
    {
        ret = MOSAL_PCI_read_config(pci_dev, offset, 4, (u_int8_t *)(&dword));
        if (ret != MT_OK)
        {
            MTL_ERROR2("Failed to read from bus=%d devfun=%d\n", pci_dev->bus->number, pci_dev->devfn);
            return(ret);
        }

        *((u_int32_t*)cfg_hdr + offset/sizeof(u_int32_t)) = dword;
    }

    if (cfg_hdr->type0.header_type == MOSAL_PCI_HEADER_TYPE0)
    {
        t0 = &cfg_hdr->type0;

        MTL_DEBUG4 ("vendor ID =                   0x%.4x\n", t0->vid);
        MTL_DEBUG4 ("device ID =                   0x%.4x\n", t0->devid);
        MTL_DEBUG4 ("command register =            0x%.4x\n", t0->cmd);
        MTL_DEBUG4 ("status register =             0x%.4x\n", t0->status); 
        MTL_DEBUG4 ("revision ID =                 0x%.2x\n", t0->revid);
        MTL_DEBUG4 ("class code =                  0x%.2x\n", t0->class_code);  
        MTL_DEBUG4 ("sub class code =              0x%.2x\n", t0->subclass);
        MTL_DEBUG4 ("programming interface =       0x%.2x\n", t0->progif);
        MTL_DEBUG4 ("cache line =                  0x%.2x\n", t0->cache_line);
        MTL_DEBUG4 ("latency time =                0x%.2x\n", t0->latency);
        MTL_DEBUG4 ("header type =                 0x%.2x\n", t0->header_type);
        MTL_DEBUG4 ("BIST =                        0x%.2x\n", t0->bist);
        MTL_DEBUG4 ("base address 0 =              0x%.8x\n", t0->base0);
        MTL_DEBUG4 ("base address 1 =              0x%.8x\n", t0->base1);   
        MTL_DEBUG4 ("base address 2 =              0x%.8x\n", t0->base2);   
        MTL_DEBUG4 ("base address 3 =              0x%.8x\n", t0->base3);   
        MTL_DEBUG4 ("base address 4 =              0x%.8x\n", t0->base4);   
        MTL_DEBUG4 ("base address 5 =              0x%.8x\n", t0->base5);   
        MTL_DEBUG4 ("cardBus CIS pointer =         0x%.8x\n", t0->cis); 
        MTL_DEBUG4 ("sub system vendor ID =        0x%.4x\n", t0->sub_vid);
        MTL_DEBUG4 ("sub system ID =               0x%.4x\n", t0->sub_sysid);
        MTL_DEBUG4 ("expansion ROM base address =  0x%.8x\n", t0->rom_base);
        MTL_DEBUG4 ("interrupt line =              0x%.2x\n", t0->int_line);
        MTL_DEBUG4 ("interrupt pin =               0x%.2x\n", t0->int_pin);
        MTL_DEBUG4 ("min Grant =                   0x%.2x\n", t0->min_grant);
        MTL_DEBUG4 ("max Latency =                 0x%.2x\n", t0->max_latency);

    } else {

        t1 = &cfg_hdr->type1;

        MTL_DEBUG4 ("vendor ID =                   0x%.4x\n", t1->vid);
        MTL_DEBUG4 ("device ID =                   0x%.4x\n", t1->devid);
        MTL_DEBUG4 ("command register =            0x%.4x\n", t1->cmd);
        MTL_DEBUG4 ("status register =             0x%.4x\n", t1->status);
        MTL_DEBUG4 ("revision ID =                 0x%.2x\n", t1->revid);
        MTL_DEBUG4 ("class code =                  0x%.2x\n", t1->class_code);
        MTL_DEBUG4 ("sub class code =              0x%.2x\n", t1->sub_class);
        MTL_DEBUG4 ("programming interface =       0x%.2x\n", t1->progif);
        MTL_DEBUG4 ("cache line =                  0x%.2x\n", t1->cache_line);
        MTL_DEBUG4 ("latency time =                0x%.2x\n", t1->latency);
        MTL_DEBUG4 ("header type =                 0x%.2x\n", t1->header_type);
        MTL_DEBUG4 ("BIST =                        0x%.2x\n", t1->bist);
        MTL_DEBUG4 ("base address 0 =              0x%.8x\n", t1->base0);
        MTL_DEBUG4 ("base address 1 =              0x%.8x\n", t1->base1);
        MTL_DEBUG4 ("primary bus number =          0x%.2x\n", t1->pri_bus);
        MTL_DEBUG4 ("secondary bus number =        0x%.2x\n", t1->sec_bus);
        MTL_DEBUG4 ("subordinate bus number =      0x%.2x\n", t1->sub_bus);
        MTL_DEBUG4 ("secondary latency timer =     0x%.2x\n", t1->sec_latency);
        MTL_DEBUG4 ("IO base =                     0x%.2x\n", t1->iobase);
        MTL_DEBUG4 ("IO limit =                    0x%.2x\n", t1->iolimit);
        MTL_DEBUG4 ("secondary status =            0x%.4x\n", t1->sec_status);
        MTL_DEBUG4 ("memory base =                 0x%.4x\n", t1->mem_base);
        MTL_DEBUG4 ("memory limit =                0x%.4x\n", t1->mem_limit);
        MTL_DEBUG4 ("prefetch memory base =        0x%.4x\n", t1->pre_base);
        MTL_DEBUG4 ("prefetch memory limit =       0x%.4x\n", t1->pre_limit);
        MTL_DEBUG4 ("prefetch memory base upper =  0x%.8x\n", t1->pre_base_upper);
        MTL_DEBUG4 ("prefetch memory limit upper = 0x%.8x\n", t1->pre_limit_upper);
        MTL_DEBUG4 ("IO base upper 16 bits =       0x%.4x\n", t1->io_base_upper);
        MTL_DEBUG4 ("IO limit upper 16 bits =      0x%.4x\n", t1->io_limit_upper);
        MTL_DEBUG4 ("expansion ROM base address =  0x%.8x\n", t1->rom_base);
        MTL_DEBUG4 ("interrupt line =              0x%.2x\n", t1->int_line);
        MTL_DEBUG4 ("interrupt pin =               0x%.2x\n", t1->int_pin);
        MTL_DEBUG4 ("bridge control =              0x%.4x\n", t1->control);

    }

	return(MT_OK);

}

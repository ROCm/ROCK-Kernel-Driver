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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/linkage.h>
#include <asm/uaccess.h>
#include <linux/pci.h>

#include <hh.h>
#include <thh.h>
#include <thh_hob.h>
#include <tavor_dev_defs.h>
#include <thh_init.h>
#include "card_defs.ic"

MODULE_LICENSE("GPL");

/*
 * Put all Tavors in device tables
 *
 */

#if LINUX_KERNEL_2_6
#define  DEV_NAME(pci_dev_p)  pci_name(pci_dev_p)
#else
#define  DEV_NAME(pci_dev_p)  (pci_dev_p)->name
#endif

#define PCI_ENUM_CONT \
      failed_init++; \
      continue

#define THHOBP(hca_hndl)   ((THH_hob_t)(hca_hndl->device))


/* if non-zero, indicates legacy sqp initialization.  May be modified by insmod parameter */
static int  thh_legacy_sqp = 0;
MODULE_PARM(thh_legacy_sqp,"i");

static int  av_in_host_mem = 0;
MODULE_PARM(av_in_host_mem,"i");

static int  infinite_cmd_timeout = 0; /* when 1 we use inifinite timeouts on commands completion */
MODULE_PARM(infinite_cmd_timeout, "i");

static int  fatal_delay_halt = 0; /* when 1, HALT_HCA on fatal is delayed to just before the reset */
MODULE_PARM(fatal_delay_halt, "i");

static int  num_cmds_outs = 0; /* max number of outstanding commands */
MODULE_PARM(num_cmds_outs, "i");

static int  async_eq_size = 0; /* size of async event queue */
MODULE_PARM(async_eq_size, "i");

static int  cmdif_use_uar0 = 0; /* when 1, cmdif posts commands to uar0 */
MODULE_PARM(cmdif_use_uar0, "i");

static int  ignore_subsystem_id = 0; /* when 1, we do not check the subsystem_vendor_id & subsystem_id */
MODULE_PARM(ignore_subsystem_id, "i");

/*
 *  card_supported
 */
static call_result_t card_supported(struct pci_dev *dev_info_p, int card_ix)
{
  u_int16_t subsystem_vendor_id, subsystem_id;
  int i;

  if (ignore_subsystem_id) {
    MTL_DEBUG1(MT_FLFMT("%s: card checks is ignored"), __func__);
    return MT_OK;
  }

  if ( pci_read_config_word(dev_info_p, PCI_SUBSYSTEM_VENDOR_ID, &subsystem_vendor_id) != 0 ) {
    return MT_ERROR;
  }

  if ( pci_read_config_word(dev_info_p, PCI_SUBSYSTEM_ID, &subsystem_id) != 0 ) {
    return MT_ERROR;
  }

  MTL_DEBUG1(MT_FLFMT("%s: subsystem_vendor_id=0x%04x, subsystem_id=0x%04x"), __func__, subsystem_vendor_id, subsystem_id);

  for ( i=0; i<(sizeof(card_defs)/sizeof(u_int16_t)); i+=2 ) {
    if ( (subsystem_vendor_id==card_defs[i]) && (subsystem_id==card_defs[i+1]) ) {
      return MT_OK;
    }
  }
  MTL_ERROR1(MT_FLFMT("%s: Tavor device InfiniHost%d (%02x:%02x.%01x) ignored: subsystem_vendor_id=0x%04x, subsystem_id=0x%04x"), 
             __func__, card_ix, dev_info_p->bus->number,PCI_SLOT(dev_info_p->devfn), PCI_FUNC(dev_info_p->devfn),
             subsystem_vendor_id, subsystem_id);
  return MT_ENORSC;

}
static void bubble(THH_hw_props_t* hw_props, MT_bool *valid_dev, int num_elts)
{
	register int i,j;
	register THH_hw_props_t temp;
    MT_bool      temp_valid;
	
	i=num_elts-1;
	while(i >= 1)
	{
		j=1;
		while(j <= i)
		{
			if((hw_props[j-1].bus >  hw_props[j].bus) ||
                ((hw_props[j-1].bus==hw_props[j].bus) && (hw_props[j-1].dev_func > hw_props[j].dev_func)))
			{  /* swap the values  */
				temp=hw_props[j-1];
				hw_props[j-1]=hw_props[j];
				hw_props[j]=temp;
                temp_valid = valid_dev[j-1];
                valid_dev[j-1]=valid_dev[j];
                valid_dev[j]=temp_valid;
			}
			j++;
		}
		i--;
	}
}

/* those two were previously defined on the stack but
   are now defined outside to reduce stack consumption */
static THH_hw_props_t hw_props[MAX_HCA_DEV_NUM];
static MT_bool        valid_dev[MAX_HCA_DEV_NUM];

static HH_ret_t THH_init_hh_all_tavor(void)
{
  HH_ret_t            ret;
  struct pci_dev      *dev_info_p= NULL;
  u_int8_t            rev_id;
  u_int32_t           failed_init = 0; /* Count number of HCA devices for which init failed */
  THH_module_flags_t  module_flags;
  u_int32_t           tavor_index = 0, create_index = 0;
  int                 found_dev_index = 0;
  u_int32_t cr_res_index,uar_res_index,ddr_res_index; /* (mem.) resources indices in PCI device */
  call_result_t rc;
  int i;
  int ven_dev_pair;
  u_int16_t vendor_id, device_id;

  cr_res_index= 0;
  uar_res_index= 2;
  ddr_res_index= 4;

  /*initialize module flags structure */
  memset(&(module_flags), 0, sizeof(THH_module_flags_t));
  memset(hw_props, 0, sizeof(hw_props));

  module_flags.legacy_sqp = (thh_legacy_sqp == 0 ? FALSE : TRUE);
  module_flags.av_in_host_mem = (av_in_host_mem == 0 ? FALSE : TRUE);
  module_flags.inifinite_cmd_timeout = (infinite_cmd_timeout==1 ? TRUE : FALSE);
  module_flags.fatal_delay_halt = (fatal_delay_halt==1 ? TRUE : FALSE);
  module_flags.cmdif_post_uar0 = cmdif_use_uar0==1 ? TRUE : FALSE;
  
  if ( num_cmds_outs == 0 ) {
    module_flags.num_cmds_outs = 0xffffffff;
  }
  else {
    module_flags.num_cmds_outs = num_cmds_outs;
  }
  
  module_flags.async_eq_size = async_eq_size;
  
  for (i = 0; i < MAX_HCA_DEV_NUM; i++) {
    valid_dev[i] = 0;
  }

  for ( ven_dev_pair=0; ven_dev_pair<(sizeof(vend_devs_defs)/sizeof(u_int16_t)); ven_dev_pair+=2 ) {
    vendor_id = vend_devs_defs[ven_dev_pair];
    device_id = vend_devs_defs[ven_dev_pair+1];
    while (TRUE) {
      dev_info_p= pci_find_device(vendor_id, device_id, dev_info_p);
      if (dev_info_p == NULL) {
        MTL_DEBUG4(MT_FLFMT("No more InfiniHosts. Found %d matching devices."), 
                   tavor_index + failed_init); 
        break;
      }
      /* pass bus number and dev_func info, for use in catastrophic error recovery */
      hw_props[found_dev_index].pci_dev = dev_info_p;
      hw_props[found_dev_index].pci_vendor_id= vendor_id;
      hw_props[found_dev_index].device_id= device_id;
      hw_props[found_dev_index].bus = dev_info_p->bus->number;
      hw_props[found_dev_index].dev_func = dev_info_p->devfn;
      MTL_DEBUG4(MT_FLFMT("InfiniHost%d: pci_find_device returned: %s @ bus=%d, dev_func=%d"), 
                 found_dev_index,DEV_NAME(dev_info_p),dev_info_p->bus->number, dev_info_p->devfn);


      rc = card_supported(dev_info_p, found_dev_index);
      if ( rc == MT_ERROR ) {
        MTL_ERROR1(MT_FLFMT("%s: failed to read from configuration space"), __func__);
        PCI_ENUM_CONT;
      }
      else if ( rc == MT_ENORSC ) {
        continue;
      }

      if ((dev_info_p->hdr_type & 0x7F) != PCI_HEADER_TYPE_NORMAL) {
        MTL_ERROR1(MT_FLFMT("Wrong PCI header type (0x%02X). Device ignored."),dev_info_p->hdr_type);
        PCI_ENUM_CONT; 
      }

      if (pci_read_config_byte(dev_info_p,PCI_REVISION_ID, &rev_id) != 0) {
        MTL_ERROR1(MT_FLFMT("Failed reading HW revision id"));
        PCI_ENUM_CONT; 
      }
      hw_props[found_dev_index].hw_ver = rev_id;

      /* get BARs */
      hw_props[found_dev_index].cr_base= dev_info_p->resource[cr_res_index].start;
      MTL_DEBUG4(MT_FLFMT("CR-space at "PHYS_ADDR_FMT" (%lu MB)"),
        hw_props[found_dev_index].cr_base,
        (dev_info_p->resource[cr_res_index].end-dev_info_p->resource[cr_res_index].start+1)>>20);
      hw_props[found_dev_index].uar_base= dev_info_p->resource[uar_res_index].start;
      MTL_DEBUG4(MT_FLFMT("UAR space at "PHYS_ADDR_FMT" (%lu MB)"),
        hw_props[found_dev_index].uar_base,
        (dev_info_p->resource[uar_res_index].end-dev_info_p->resource[uar_res_index].start+1)>>20);
      hw_props[found_dev_index].ddr_base= dev_info_p->resource[ddr_res_index].start;
      MTL_DEBUG4(MT_FLFMT("Attached DDR memory at "PHYS_ADDR_FMT" (%lu MB)"),
        hw_props[found_dev_index].ddr_base,
        (dev_info_p->resource[ddr_res_index].end-dev_info_p->resource[ddr_res_index].start+1)>>20);

      /* Get interrupt properties */
      /* OPTERON WorkAround:
         hw_props.interrupt_props.irq= dev_info_p->irq-1; */
      hw_props[found_dev_index].interrupt_props.irq= dev_info_p->irq;
      if (pci_read_config_byte(dev_info_p, PCI_INTERRUPT_PIN, 
            &(hw_props[found_dev_index].interrupt_props.intr_pin)) !=0) {
        MTL_ERROR1(MT_FLFMT("Failed reading HW revision id"));
        PCI_ENUM_CONT; 
      }

      MTL_DEBUG4(MT_FLFMT("Interrupt pin %d routed to IRQ %d"),
        hw_props[found_dev_index].interrupt_props.intr_pin,hw_props[found_dev_index].interrupt_props.irq);

      /* Enable "master" and "memory" flags */
      pci_set_master(dev_info_p);
      if (pci_enable_device(dev_info_p) != 0) {
        MTL_ERROR1(MT_FLFMT("Failed enabling PCI device: %s"),DEV_NAME(dev_info_p));
        PCI_ENUM_CONT; 
      }
      valid_dev[found_dev_index] = 1;
      tavor_index++;
      found_dev_index++;
    }
  }


  if (tavor_index == 0 ) {
    if (failed_init > 0) {
      MTL_ERROR1("%s: For all %d Tavor devices initialization was not successful\n", __func__,failed_init);
      return(HH_ERR);
    }
    else { /* failed_init == 0 */
      MTL_ERROR4("%s: No Tavor devices were found.\n", __func__); 
      /* Maybe this is not a real error - but important enouth to log */
      return(MT_OK);
    }
  }
  /* sort the found tavor devices by bus number */
  bubble(hw_props,valid_dev,found_dev_index);

  /* Create the Tavor HOB objects */
  for (create_index = 0, i = 0; i < found_dev_index; i++) {
      if (valid_dev[i] == 0) {
          continue;
      }
      MTL_TRACE1("%s: calling THH_hob_create: InfiniHost%d\n", __func__, i);
      ret = THH_hob_create(&(hw_props[i]), i, &(module_flags), NULL);
      if (ret != HH_OK) {
        MTL_ERROR1(MT_FLFMT("Failed creating THH_hob for InfiniHost%d"),i);
        failed_init++; 
      } else {
        create_index++;
      }
  }

  if (create_index == 0) {
    MTL_ERROR1("%s: For all %d Tavor devices initialization was not successful\n", __func__,failed_init);
    return(HH_ERR);
  }
  else { /* created at least one Tavor device */
    MTL_DEBUG1(MT_FLFMT("%d HCAs were successfully initialized."),create_index); 
    return(MT_OK);
  }
}


/*
 *  THH_module_init
 */
static int THH_module_init(void)
{
  HH_ret_t  ret;

  MTL_TRACE1(MT_FLFMT("THH's init_module() called\n"));

  /* static context initialization */
  ret = THH_init();
  if (ret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("THH_init failed (%s)\n"), HH_strerror_sym(ret));
    return(-1);
  }

  /* Enumerate PCI bus to find Tavors, initialize and create THH context for each */
  ret = THH_init_hh_all_tavor();
  if (ret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("Failed initialization of all available InfiniHost devices \n")); 
    return(-1);
  }

  printk("<1>THH kernel module initialized successfully\n");

  return 0;
}


/*
 *  THH_module_exit
 */
static void THH_module_exit(void)
{
  HH_ret_t        ret;
  HH_hca_hndl_t   *hca_list_buf_p;
  u_int32_t       num_hcas = 0;
  int             i;
  int             ven_dev_pair;
  char            devname[32];
  MT_bool         to_destroy;

  /* get handles for all active HCA devices (tavors, gamlas, etc) */
  if ((ret=HH_list_hcas(0,&num_hcas,NULL)) != HH_EAGAIN)
      return;   /* cannot clean up */

  if (num_hcas == 0) {
      goto cleanup;
  }
  
  hca_list_buf_p = TNMALLOC(HH_hca_hndl_t, num_hcas);
  if (hca_list_buf_p == NULL) {
      MTL_ERROR1(MT_FLFMT("cleanup_module: malloc failure"));
      goto cleanup;
  }

  if ((ret=HH_list_hcas(num_hcas,&num_hcas,hca_list_buf_p)) != HH_OK){
      MTL_ERROR1(MT_FLFMT("cleanup_module: failed to get list of HCAs"));
      FREE(hca_list_buf_p);
      return;   /* cannot clean up */
  }

  /* Destroy each Tavor device in the table and disable PCI memory BARs */
  for (i= 0; i < num_hcas; i++)  {
    to_destroy = FALSE;
    /* check for tavor or arbel */
    for ( ven_dev_pair=0; ven_dev_pair<(sizeof(vend_devs_defs)/sizeof(u_int16_t)); ven_dev_pair+=2 ) {
      if (hca_list_buf_p[i]->dev_id == vend_devs_defs[ven_dev_pair+1]) {
        to_destroy = TRUE;
        break; 
      }
    }
    if (!to_destroy) {
        continue;
    }

    strcpy(devname,((HH_hca_hndl_t)(hca_list_buf_p[i]))->dev_desc);
    MTL_DEBUG3(MT_FLFMT("cleanup_module: removing the device %s"),devname);
    MTL_ERROR1(MT_FLFMT("cleanup_module: destroying %s"),devname);
    ret = THH_hob_destroy((HH_hca_hndl_t)(hca_list_buf_p[i]));
    if (ret != HH_OK) {
      MTL_ERROR1(MT_FLFMT("cleanup_module: Failed THH_hob_destroy for %s (%s)\n"),
        devname, HH_strerror_sym(ret));
    }
  }
  FREE(hca_list_buf_p);

  /* static context cleanup */
cleanup:  
  ret = THH_cleanup();
  if (ret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("cleanup_module:  THH_cleanup failed (%s)"), HH_strerror_sym(ret));
    return;
  }
  printk("<1>THH kernel module removed successfully\n");
  return;
}

module_init(THH_module_init);
module_exit(THH_module_exit);

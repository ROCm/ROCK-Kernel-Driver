/************************************************************************/
/* File iSeries pci_proc.c created by Allan Trautman on Feb 27 2001.    */
/************************************************************************/
/* Create /proc/iSeries/pci file that contains iSeries card location.   */
/* Copyright (C) 20yy  <Allan H Trautman> <IBM Corp>                    */
/*                                                                      */
/* This program is free software; you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation; either version 2 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* This program is distributed in the hope that it will be useful,      */ 
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU General Public License for more details.                         */
/*                                                                      */
/* You should have received a copy of the GNU General Public License    */ 
/* along with this program; if not, write to the:                       */
/* Free Software Foundation, Inc.,                                      */ 
/* 59 Temple Place, Suite 330,                                          */ 
/* Boston, MA  02111-1307  USA                                          */
/************************************************************************/
/* Change Activity:                                                     */
/*   Created, Feb 27, 2001                                              */
/* End Change Activity                                                  */
/************************************************************************/
#include <asm/uaccess.h>
#include <asm/iSeries/iSeries_VpdInfo.h>
#include <asm/iSeries/iSeries_proc.h>
#include <asm/iSeries/iSeries_pci.h>
#include <asm/iSeries/iSeries_FlightRecorder.h>
#include <linux/pci.h>
#include <linux/netdevice.h>

static struct proc_dir_entry *pci_proc_root   = NULL;
static struct proc_dir_entry *pciFr_proc_root = NULL;

int iSeries_proc_pci_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data);
int iSeries_proc_pci_write_proc(struct file *file, const char *buffer, unsigned long count, void *data);
int iSeries_proc_pciFr_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data);
int iSeries_proc_pciFr_write_proc(struct file *file, const char *buffer, unsigned long count, void *data);


void iSeries_pci_proc_init(struct proc_dir_entry *iSeries_proc) {
    printk("PCI: Creating /proc/iSeries/pci\n");

    /* Read = User,Group,Other, Write User */ 
    pci_proc_root = create_proc_entry("pci",    S_IFREG | S_IRUGO | S_IWUSR, iSeries_proc);
    if (!pci_proc_root) return;
    pci_proc_root->nlink = 1;
    pci_proc_root->data = (void *)0;
    pci_proc_root->read_proc  = iSeries_proc_pci_read_proc;
    pci_proc_root->write_proc = iSeries_proc_pci_write_proc;

    /* Read = User,Group,Other, Write User */ 
    printk("PCI: Creating /proc/iSeries/pciFr\n");
    pciFr_proc_root = create_proc_entry("pciFr", S_IFREG | S_IRUGO | S_IWUSR, iSeries_proc);
    if (!pciFr_proc_root) return;
    pciFr_proc_root->nlink = 1;
    pciFr_proc_root->data = (void *)0;
    pciFr_proc_root->read_proc  = iSeries_proc_pciFr_read_proc;
    pciFr_proc_root->write_proc = iSeries_proc_pciFr_write_proc;
}

/*******************************************************************************/
/* Get called when client reads the /proc/iSeries/pci file.  The data returned */
/* is the iSeries card locations for service.                                  */
/*******************************************************************************/
int iSeries_proc_pci_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data) {
    int               LineLen;			/* Size of new Data            */
    struct  pci_dev*  PciDev;			/* Device pointer              */
    struct net_device *dev;			/* net_device pointer          */
    int               DeviceCount;              /* Device Number               */
    /***************************************************************************/
    /* ###        Bus  Device      Bus  Dev Frm Card                           */
    /*   1. Linux:  0/ 28  iSeries: 24/ 36/  1/C14                             */
    /*   2. Linux:  0/ 30  iSeries: 24/ 38/  2/C14                             */
    /***************************************************************************/
    DeviceCount = 1;			/* Count the devices listed.           */
    LineLen     = 0;			/* Reset Length                        */

    /***************************************************************************/
    /* List the devices                                                        */
    /***************************************************************************/
    pci_for_each_dev(PciDev) {
	LineLen += sprintf(page+LineLen,"%3d. ",DeviceCount);
	LineLen += iSeries_Device_Information(PciDev,page+LineLen,count-LineLen);
	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
	    if (dev->base_addr == PciDev->resource[0].start ) { /* Yep, a net_device */
		LineLen += sprintf(page+LineLen, ", Net device: %s", dev->name);
	    } /* if */
	} /* for */
	LineLen += sprintf(page+LineLen,"\n");
 	++DeviceCount;			/* Add for the list.                    */
        /************************************************************************/
	/* Run out of room in system buffer.                                    */
        /************************************************************************/
	if(LineLen+80 >= count) {	/* Room for another line?  No. bail out */
	    LineLen +=sprintf(page+LineLen,"/proc/pci file full!\n");
	    break;
	}
    }
    /***************************************************************************/
    /* If no devices, tell user that instead                                   */
    /***************************************************************************/
    if(DeviceCount == 1) {
	LineLen +=sprintf(page+LineLen,"No PCI devices found\n");
    }
    /***************************************************************************/
    /* Update counts and return                                                */
    /***************************************************************************/
    *eof = LineLen;
    return LineLen;			
}

/*******************************************************************************/
/* Do nothing, Nothing to support for the write                                */
/*******************************************************************************/
int iSeries_proc_pci_write_proc(struct file *file, const char *buffer, unsigned long count, void *data) {
    return count;
}

/*******************************************************************************/
/* Get called when client reads the /proc/iSeries/pci file.  The data returned */
/* is the iSeries card locations for service.                                  */
/*******************************************************************************/
int iSeries_proc_pciFr_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data) {
    struct  pci_dev*  PciDev;			/* Device pointer              */
    int               DeviceCount = 1;          /* Device Number               */
    int               LineLen     = 0;		/* Size of new Data            */

    printk("PCI: Dump Flight Recorder!\n");
    /***************************************************************************/
    /* List the devices                                                        */
    /***************************************************************************/
    pci_for_each_dev(PciDev) {
	LineLen += sprintf(page+LineLen,"%3d. 0x%08X  ",DeviceCount,(int)PciDev);
	LineLen += sprintf(page+LineLen,"Bus: %02X, Device: %02X ",PciDev->bus->number,PciDev->devfn);
	LineLen += sprintf(page+LineLen,"\n");
	++DeviceCount;			/* Add for the list.                    */
    }
    LineLen += sprintf(page+LineLen,"--\n");
    /***************************************************************************/
    /* The Flight Recorder                                                     */
    /* Someday handle wrap                                                     */
    /***************************************************************************/
    if (PciFr->StartingPointer != NULL) {
	char* StartEntry = (char*)PciFr->StartingPointer;
	char* EndEntry   = (char*)PciFr->CurrentPointer;
	while(EndEntry > StartEntry && LineLen+40 < count) {
	    LineLen += sprintf(page+LineLen,"%s\n",StartEntry);
	    StartEntry += strlen(StartEntry) + 1;
	}
	if(LineLen+40 >= count) {
            printk("PCI: Max Count Hit %d and %d\n",LineLen,count);
        }
    }
    /***************************************************************************/
    /* Update counts and return                                                */
    /***************************************************************************/
    printk("PCI: End of File at %d\n",LineLen);
    *eof = LineLen;
    return LineLen;			
}
/*******************************************************************************/
/* Flight Recorder Controls                                                    */
/*******************************************************************************/
int iSeries_proc_pciFr_write_proc(struct file *file, const char *buffer, unsigned long count, void *data) {
    if(buffer != 0 && strlen(buffer) > 0) {
	if(     strstr(buffer,"trace on")  != NULL) {
	    iSeries_Set_PciTraceFlag(1);
            ISERIES_PCI_FR_DATE("PCI Trace turned on!");
	}
	else if(strstr(buffer,"trace off") != NULL) {
	    iSeries_Set_PciTraceFlag(0);
            ISERIES_PCI_FR_DATE("PCI Trace turned off!");
	}
        else {
            ISERIES_PCI_FR_TIME("PCI Trace Option Invalid!");
            readl(0x00000000);
        }
    }
    return count;
}

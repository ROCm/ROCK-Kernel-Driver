/* File iSeries_ksyms.c created by root on Tue Feb 13 2001. */

/* Change Activity: */
/* End Change Activity */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/iSeries/iSeries_dma.h>
#include <asm/iSeries/mf.h>
#include <asm/iSeries/HvCallSc.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/Naca.h>
#include <asm/iSeries/iSeries_proc.h>
#include <asm/iSeries/ItLpNaca.h>
#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/iSeries_io.h>
#include <asm/iSeries/iSeries_FlightRecorder.h>
#include <asm/iSeries/iSeries_pci.h>
#include <asm/iSeries/iSeries_VpdInfo.h>
#include <asm/delay.h> 

EXPORT_SYMBOL(HvLpEvent_registerHandler);
EXPORT_SYMBOL(HvLpEvent_unregisterHandler);
EXPORT_SYMBOL(HvLpEvent_openPath);
EXPORT_SYMBOL(HvLpEvent_closePath);

EXPORT_SYMBOL(HvCall1);
EXPORT_SYMBOL(HvCall2);
EXPORT_SYMBOL(HvCall3);
EXPORT_SYMBOL(HvCall4);
EXPORT_SYMBOL(HvCall5);
EXPORT_SYMBOL(HvCall6);
EXPORT_SYMBOL(HvCall7);
EXPORT_SYMBOL(HvCall0);
EXPORT_SYMBOL(HvCall0Ret16);
EXPORT_SYMBOL(HvCall1Ret16);
EXPORT_SYMBOL(HvCall2Ret16);
EXPORT_SYMBOL(HvCall3Ret16);
EXPORT_SYMBOL(HvCall4Ret16);
EXPORT_SYMBOL(HvCall5Ret16);
EXPORT_SYMBOL(HvCall6Ret16);
EXPORT_SYMBOL(HvCall7Ret16);

EXPORT_SYMBOL(HvLpConfig_getLpIndex_outline);
EXPORT_SYMBOL(virt_to_absolute_outline);

EXPORT_SYMBOL(mf_allocateLpEvents);
EXPORT_SYMBOL(mf_deallocateLpEvents);

EXPORT_SYMBOL(iSeries_proc_callback);


#ifdef CONFIG_PCI
EXPORT_SYMBOL(pci_map_single);
EXPORT_SYMBOL(pci_unmap_single);

EXPORT_SYMBOL(iSeries_Readb);
EXPORT_SYMBOL(iSeries_Readw);
EXPORT_SYMBOL(iSeries_Readl);
EXPORT_SYMBOL(iSeries_Writeb);
EXPORT_SYMBOL(iSeries_Writew);
EXPORT_SYMBOL(iSeries_Writel);

EXPORT_SYMBOL(iSeries_memcpy_fromio);
EXPORT_SYMBOL(iSeries_memcpy_toio);

EXPORT_SYMBOL(iSeries_GetLocationData);

EXPORT_SYMBOL(iSeries_Set_PciTraceFlag);
EXPORT_SYMBOL(iSeries_Get_PciTraceFlag);

EXPORT_SYMBOL(iSeries_Device_Reset_NoIrq);
EXPORT_SYMBOL(iSeries_Device_Reset_Generic);
EXPORT_SYMBOL(iSeries_Device_Reset);
EXPORT_SYMBOL(iSeries_Device_RestoreConfigRegs);
EXPORT_SYMBOL(iSeries_Device_SaveConfigRegs);
EXPORT_SYMBOL(iSeries_Device_ToggleReset);
#endif


  

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

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: client_query_main.c 32 2004-04-09 03:57:42Z roland $
*/

#include "client_query.h"
#include "ts_ib_mad.h"
#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/param.h>

#include <asm/system.h>
#else
#include <os_dep/win/linux/module.h>
#endif

#ifdef W2K_OS // Vipul
#include <ts_ib_init.h>
#endif

MODULE_AUTHOR("Carl Yang");
MODULE_DESCRIPTION("kernel IB client query service");
MODULE_LICENSE("Dual BSD/GPL");

enum {
  TS_IB_CLIENT_MAX_MGMT_CLASS = 1 << 8
};

static struct {
  tTS_IB_MAD_DISPATCH_FUNCTION function;
  void                        *arg;
  tTS_IB_MAD_FILTER_HANDLE     filter;
} async_mad_table[TS_IB_CLIENT_MAX_MGMT_CLASS];

int tsIbClientMadHandlerRegister(
                                 uint8_t mgmt_class,
                                 tTS_IB_MAD_DISPATCH_FUNCTION function,
                                 void *arg
                                 )
{
  int ret;

  if (function) {
    tTS_IB_MAD_FILTER_STRUCT filter = { 0 };

    filter.qpn        = 1;
    filter.mgmt_class = mgmt_class;
    filter.direction  = TS_IB_MAD_DIRECTION_IN;
    filter.mask       = TS_IB_MAD_FILTER_QPN | TS_IB_MAD_FILTER_MGMT_CLASS |
                        TS_IB_MAD_FILTER_DIRECTION;
    snprintf(filter.name, sizeof filter.name, "query client (class 0x%02x)",
             mgmt_class);

    ret = tsIbMadHandlerRegister(&filter,
                                 tsIbClientMadHandler,
                                 NULL,
                                 &async_mad_table[mgmt_class].filter);
    if (ret) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Failed to register MAD filter");
      return ret;
    }
  } else {
    tsIbMadHandlerDeregister(async_mad_table[mgmt_class].filter);
  }

  async_mad_table[mgmt_class].function = function;
  async_mad_table[mgmt_class].arg      = arg;

  return 0;
}

tTS_IB_MAD_DISPATCH_FUNCTION tsIbClientAsyncMadHandlerGet(
                                                          uint8_t mgmt_class
                                                          )
{
  return async_mad_table[mgmt_class].function;
}

void *tsIbClientAsynMadHandlerArgGet(
                                     uint8_t mgmt_class
                                     )
{
  return async_mad_table[mgmt_class].arg;
}

static int __init _tsIbClientQueryInitModule(void) {
  if (tsIbClientQueryInit()) {
    return -EINVAL;
  }

  return 0;
}

static void __exit _tsIbClientQueryCleanupModule(void) {
  tsIbClientQueryCleanup();
}

#ifdef W2K_OS // Vipul
#define DRIVER_NT_DEV_NAME      L"\\Device\\tsapi_client_query"
#define DRIVER_DOS_DEV_NAME     L"\\DosDevices\\tsapi_client_query"

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING UniRegistryPath);
VOID DriverUnload(IN PDRIVER_OBJECT DriverObject);

typedef struct _DEVICE_EXTENSION {

        PDEVICE_OBJECT pDevice;

        /* Add ofhter device specific members here */

} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

/************************************************************
    InitializeModule

    Gets called by the IB Device driver after it has been
    completely initialized.
 ************************************************************/

BOOLEAN bModuleInitialized = FALSE;

BOOLEAN
InitializeModule( void* pParam )
{
    /* don't initialize if it has already been done */
    if( bModuleInitialized == FALSE )
    {
        bModuleInitialized = ( _tsIbClientQueryInitModule() == 0 );
    }

    return( bModuleInitialized );
}

NTSTATUS
DriverEntry(IN PDRIVER_OBJECT DriverObject,
            IN PUNICODE_STRING UniRegistryPath) {

    NTSTATUS                status;
    PDEVICE_OBJECT          pDevObj;
    PDEVICE_EXTENSION       pDevExt;
    UNICODE_STRING          sNTName;
    UNICODE_STRING          sWin32Name;

    RtlInitUnicodeString(&sNTName, DRIVER_NT_DEV_NAME);

    /* Create the device object */
    status = IoCreateDevice(DriverObject,
                    sizeof(DEVICE_EXTENSION),
                    &sNTName,
                    FILE_DEVICE_UNKNOWN,
                    0,
                    FALSE,
                    &pDevObj);

    if (!(NT_SUCCESS(status))) {
            return(status);
    }

    /* Initialize the Device Extension */
    pDevExt = (PDEVICE_EXTENSION)pDevObj->DeviceExtension;
    pDevExt->pDevice = pDevObj;

    /* Use buffered I/O */
    pDevObj->Flags |= DO_BUFFERED_IO;

    /* Create the symbolic link for the driver */
    RtlInitUnicodeString(&sWin32Name, DRIVER_DOS_DEV_NAME);
    status = IoCreateSymbolicLink(&sWin32Name, &sNTName);

    if (!(NT_SUCCESS(status))) {
            IoDeleteDevice(pDevObj);
            return(status);
    }

    /* Setup Driver Functions */
    DriverObject->DriverUnload = DriverUnload;

    tsIbRegisterInit( "ts_ib_client_query", InitializeModule, NULL, 5 );

	return( STATUS_SUCCESS );

}

VOID
DriverUnload(IN PDRIVER_OBJECT DriverObject) {

        UNICODE_STRING  sWin32Name;

        RtlInitUnicodeString(&sWin32Name, DRIVER_DOS_DEV_NAME);

        IoDeleteSymbolicLink(&sWin32Name);
        IoDeleteDevice(DriverObject->DeviceObject);

        _tsIbClientQueryCleanupModule();
}
#endif // W2K_OS

module_init(_tsIbClientQueryInitModule);
module_exit(_tsIbClientQueryCleanupModule);

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

  $Id: dm_client_main.c 32 2004-04-09 03:57:42Z roland $
*/

#include "dm_client.h"

#include "ts_ib_mad.h"
#include "ts_ib_dm_client.h"
#include "ts_ib_dm_client_host.h"
#include "ts_ib_client_query.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS // Vipul
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
#include <ts_ib_init.h>
#endif
int use_port_info_tbl = 1;

MODULE_PARM (use_port_info_tbl, "i");


MODULE_AUTHOR("Carl Yang");
MODULE_DESCRIPTION("kernel IB Device Managment client");
MODULE_LICENSE("Dual BSD/GPL");

static int __init _tsIbDmClientInitModule(void)
{
  if (tsIbDmClientQueryInit()) {
    return -EINVAL;
  }

  tsIbClientMadHandlerRegister(TS_IB_MGMT_CLASS_DEV_MGT,
                               tsIbDmAsyncNotifyHandler,
                               NULL);

  return 0;
}

static void __exit _tsIbDmClientCleanupModule(void) {
  tsIbClientMadHandlerRegister(TS_IB_MGMT_CLASS_DEV_MGT, NULL, NULL);

  tsIbDmClientQueryCleanup();
}

#ifdef W2K_OS // Vipul

#define DRIVER_NT_DEV_NAME      L"\\Device\\tsapi_dm_client"
#define DRIVER_DOS_DEV_NAME     L"\\DosDevices\\tsapi_dm_client"

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
        bModuleInitialized = ( _tsIbDmClientInitModule() == 0 );
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

    tsIbRegisterInit( "ts_ib_dm_client", InitializeModule, NULL, 6 );

    return( STATUS_SUCCESS );
}

VOID
DriverUnload(IN PDRIVER_OBJECT DriverObject) {

        UNICODE_STRING  sWin32Name;

        RtlInitUnicodeString(&sWin32Name, DRIVER_DOS_DEV_NAME);

        IoDeleteSymbolicLink(&sWin32Name);
        IoDeleteDevice(DriverObject->DeviceObject);

        _tsIbDmClientCleanupModule();
}
#endif

module_init(_tsIbDmClientInitModule);
module_exit(_tsIbDmClientCleanupModule);

/*****************************************************************************/
/* ips.h -- driver for the IBM ServeRAID controller                          */
/*                                                                           */
/* Written By: Keith Mitchell, IBM Corporation                               */
/*                                                                           */
/* Copyright (C) 1999 IBM Corporation                                        */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation; either version 2 of the License, or         */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* NO WARRANTY                                                               */
/* THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR        */
/* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT      */
/* LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,      */
/* MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is    */
/* solely responsible for determining the appropriateness of using and       */
/* distributing the Program and assumes all risks associated with its        */
/* exercise of rights under this Agreement, including but not limited to     */
/* the risks and costs of program errors, damage to or loss of data,         */
/* programs or equipment, and unavailability or interruption of operations.  */
/*                                                                           */
/* DISCLAIMER OF LIABILITY                                                   */
/* NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY   */
/* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL        */
/* DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND   */
/* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR     */
/* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE    */
/* USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED  */
/* HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES             */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
/*                                                                           */
/* Bugs/Comments/Suggestions should be mailed to:                            */
/*      ipslinux@us.ibm.com                                                  */
/*                                                                           */
/*****************************************************************************/

#ifndef _IPS_H_
   #define _IPS_H_

   #include <asm/uaccess.h>
   #include <asm/io.h>

   /* type definitions */
   #define u_int8_t  uint8_t
   #define u_int16_t uint16_t
   #define u_int32_t uint32_t
   #define u_int64_t uint64_t

   /* Prototypes */
   extern int ips_detect(Scsi_Host_Template *);
   extern int ips_release(struct Scsi_Host *);
   extern int ips_eh_abort(Scsi_Cmnd *);
   extern int ips_eh_reset(Scsi_Cmnd *);
   extern int ips_queue(Scsi_Cmnd *, void (*) (Scsi_Cmnd *));
   extern int ips_biosparam(Disk *, kdev_t, int *);
   extern const char * ips_info(struct Scsi_Host *);
   extern void do_ipsintr(int, void *, struct pt_regs *);

   /*
    * Some handy macros
    */
   #ifndef LinuxVersionCode
      #define LinuxVersionCode(x,y,z)  (((x)<<16)+((y)<<8)+(z))
   #endif

   #define IPS_HA(x)                   ((ips_ha_t *) x->hostdata)
   #define IPS_COMMAND_ID(ha, scb)     (int) (scb - ha->scbs)
   #define IPS_IS_TROMBONE(ha)         (((ha->device_id == IPS_DEVICEID_COPPERHEAD) && \
                                         (ha->revision_id >= IPS_REVID_TROMBONE32) && \
                                         (ha->revision_id <= IPS_REVID_TROMBONE64)) ? 1 : 0)
   #define IPS_IS_CLARINET(ha)         (((ha->device_id == IPS_DEVICEID_COPPERHEAD) && \
                                         (ha->revision_id >= IPS_REVID_CLARINETP1) && \
                                         (ha->revision_id <= IPS_REVID_CLARINETP3)) ? 1 : 0)
   #define IPS_IS_MORPHEUS(ha)         (ha->device_id == IPS_DEVICEID_MORPHEUS)
   #define IPS_USE_I2O_DELIVER(ha)     ((IPS_IS_MORPHEUS(ha) || \
                                         (IPS_IS_TROMBONE(ha) && \
                                          (ips_force_i2o))) ? 1 : 0)
   #define IPS_USE_I2O_STATUS(ha)      (IPS_IS_MORPHEUS(ha))
   #define IPS_USE_MEMIO(ha)           ((IPS_IS_MORPHEUS(ha) || \
                                         ((IPS_IS_TROMBONE(ha) || IPS_IS_CLARINET(ha)) && \
                                          (ips_force_memio))) ? 1 : 0)

   #ifndef VIRT_TO_BUS
      #define VIRT_TO_BUS(x)           (unsigned int)virt_to_bus((void *) x)
   #endif

   #ifndef UDELAY
      #define UDELAY udelay
   #endif

   #ifndef MDELAY
      #define MDELAY mdelay
   #endif

   #ifndef verify_area_20
      #define verify_area_20(t,a,sz)   (0) /* success */
   #endif

   #ifndef DECLARE_MUTEX_LOCKED
      #define DECLARE_MUTEX_LOCKED(sem) struct semaphore sem = MUTEX_LOCKED;
   #endif
   
   /*
    * Lock macros
    */
   #define IPS_SCB_LOCK(cpu_flags)      spin_lock_irqsave(&ha->scb_lock, cpu_flags)
   #define IPS_SCB_UNLOCK(cpu_flags)    spin_unlock_irqrestore(&ha->scb_lock, cpu_flags)
   #define IPS_QUEUE_LOCK(queue)        spin_lock_irqsave(&(queue)->lock, (queue)->cpu_flags)
   #define IPS_QUEUE_UNLOCK(queue)      spin_unlock_irqrestore(&(queue)->lock, (queue)->cpu_flags)
   #define IPS_HA_LOCK(cpu_flags)       spin_lock_irqsave(&ha->ips_lock, cpu_flags)
   #define IPS_HA_UNLOCK(cpu_flags)     spin_unlock_irqrestore(&ha->ips_lock, cpu_flags)

   /*
    * Adapter address map equates
    */
   #define IPS_REG_HISR                 0x08    /* Host Interrupt Status Reg   */
   #define IPS_REG_CCSAR                0x10    /* Cmd Channel System Addr Reg */
   #define IPS_REG_CCCR                 0x14    /* Cmd Channel Control Reg     */
   #define IPS_REG_SQHR                 0x20    /* Status Q Head Reg           */
   #define IPS_REG_SQTR                 0x24    /* Status Q Tail Reg           */
   #define IPS_REG_SQER                 0x28    /* Status Q End Reg            */
   #define IPS_REG_SQSR                 0x2C    /* Status Q Start Reg          */
   #define IPS_REG_SCPR                 0x05    /* Subsystem control port reg  */
   #define IPS_REG_ISPR                 0x06    /* interrupt status port reg   */
   #define IPS_REG_CBSP                 0x07    /* CBSP register               */
   #define IPS_REG_FLAP                 0x18    /* Flash address port          */
   #define IPS_REG_FLDP                 0x1C    /* Flash data port             */
   #define IPS_REG_NDAE                 0x38    /* Anaconda 64 NDAE Register   */
   #define IPS_REG_I2O_INMSGQ           0x40    /* I2O Inbound Message Queue   */
   #define IPS_REG_I2O_OUTMSGQ          0x44    /* I2O Outbound Message Queue  */
   #define IPS_REG_I2O_HIR              0x30    /* I2O Interrupt Status        */
   #define IPS_REG_I960_IDR             0x20    /* i960 Inbound Doorbell       */
   #define IPS_REG_I960_MSG0            0x18    /* i960 Outbound Reg 0         */
   #define IPS_REG_I960_MSG1            0x1C    /* i960 Outbound Reg 1         */
   #define IPS_REG_I960_OIMR            0x34    /* i960 Oubound Int Mask Reg   */

   /*
    * Adapter register bit equates
    */
   #define IPS_BIT_GHI                  0x04    /* HISR General Host Interrupt */
   #define IPS_BIT_SQO                  0x02    /* HISR Status Q Overflow      */
   #define IPS_BIT_SCE                  0x01    /* HISR Status Channel Enqueue */
   #define IPS_BIT_SEM                  0x08    /* CCCR Semaphore Bit          */
   #define IPS_BIT_ILE                  0x10    /* CCCR ILE Bit                */
   #define IPS_BIT_START_CMD            0x101A  /* CCCR Start Command Channel  */
   #define IPS_BIT_START_STOP           0x0002  /* CCCR Start/Stop Bit         */
   #define IPS_BIT_RST                  0x80    /* SCPR Reset Bit              */
   #define IPS_BIT_EBM                  0x02    /* SCPR Enable Bus Master      */
   #define IPS_BIT_EI                   0x80    /* HISR Enable Interrupts      */
   #define IPS_BIT_OP                   0x01    /* OP bit in CBSP              */
   #define IPS_BIT_I2O_OPQI             0x08    /* General Host Interrupt      */
   #define IPS_BIT_I960_MSG0I           0x01    /* Message Register 0 Interrupt*/
   #define IPS_BIT_I960_MSG1I           0x02    /* Message Register 1 Interrupt*/

   /*
    * Adapter Command ID Equates
    */
   #define IPS_CMD_GET_LD_INFO          0x19
   #define IPS_CMD_GET_SUBSYS           0x40
   #define IPS_CMD_READ_CONF            0x38
   #define IPS_CMD_RW_NVRAM_PAGE        0xBC
   #define IPS_CMD_READ                 0x02
   #define IPS_CMD_WRITE                0x03
   #define IPS_CMD_FFDC                 0xD7
   #define IPS_CMD_ENQUIRY              0x05
   #define IPS_CMD_FLUSH                0x0A
   #define IPS_CMD_READ_SG              0x82
   #define IPS_CMD_WRITE_SG             0x83
   #define IPS_CMD_DCDB                 0x04
   #define IPS_CMD_DCDB_SG              0x84
   #define IPS_CMD_CONFIG_SYNC          0x58
   #define IPS_CMD_ERROR_TABLE          0x17
   #define IPS_CMD_RW_BIOSFW            0x22

   /*
    * Adapter Equates
    */
   #define IPS_CSL                      0xFF
   #define IPS_POCL                     0x30
   #define IPS_NORM_STATE               0x00
   #define IPS_MAX_ADAPTERS             16
   #define IPS_MAX_IOCTL                1
   #define IPS_MAX_IOCTL_QUEUE          8
   #define IPS_MAX_QUEUE                128
   #define IPS_BLKSIZE                  512
   #define IPS_MAX_SG                   17
   #define IPS_MAX_LD                   8
   #define IPS_MAX_CHANNELS             4
   #define IPS_MAX_TARGETS              15
   #define IPS_MAX_CHUNKS               16
   #define IPS_MAX_CMDS                 128
   #define IPS_MAX_XFER                 0x10000
   #define IPS_NVRAM_P5_SIG             0xFFDDBB99
   #define IPS_MAX_POST_BYTES           0x02
   #define IPS_MAX_CONFIG_BYTES         0x02
   #define IPS_GOOD_POST_STATUS         0x80
   #define IPS_SEM_TIMEOUT              2000
   #define IPS_IOCTL_COMMAND            0x0D
   #define IPS_IOCTL_NEW_COMMAND        0x81
   #define IPS_INTR_ON                  0
   #define IPS_INTR_IORL                1
   #define IPS_INTR_HAL                 2
   #define IPS_ADAPTER_ID               0xF
   #define IPS_VENDORID                 0x1014
   #define IPS_DEVICEID_COPPERHEAD      0x002E
   #define IPS_DEVICEID_MORPHEUS        0x01BD
   #define IPS_SUBDEVICEID_4M           0x01BE
   #define IPS_SUBDEVICEID_4L           0x01BF
   #define IPS_SUBDEVICEID_4MX          0x0208
   #define IPS_SUBDEVICEID_4LX          0x020E
   #define IPS_IOCTL_SIZE               8192
   #define IPS_STATUS_SIZE              4
   #define IPS_STATUS_Q_SIZE            (IPS_MAX_CMDS+1) * IPS_STATUS_SIZE
   #define IPS_IMAGE_SIZE               500 * 1024
   #define IPS_MEMMAP_SIZE              128
   #define IPS_ONE_MSEC                 1
   #define IPS_ONE_SEC                  1000

   /*
    * Geometry Settings
    */
   #define IPS_COMP_HEADS               128
   #define IPS_COMP_SECTORS             32
   #define IPS_NORM_HEADS               254
   #define IPS_NORM_SECTORS             63

   /*
    * Adapter Basic Status Codes
    */
   #define IPS_BASIC_STATUS_MASK        0xFF
   #define IPS_GSC_STATUS_MASK          0x0F
   #define IPS_CMD_SUCCESS              0x00
   #define IPS_CMD_RECOVERED_ERROR      0x01
   #define IPS_INVAL_OPCO               0x03
   #define IPS_INVAL_CMD_BLK            0x04
   #define IPS_INVAL_PARM_BLK           0x05
   #define IPS_BUSY                     0x08
   #define IPS_CMD_CMPLT_WERROR         0x0C
   #define IPS_LD_ERROR                 0x0D
   #define IPS_CMD_TIMEOUT              0x0E
   #define IPS_PHYS_DRV_ERROR           0x0F

   /*
    * Adapter Extended Status Equates
    */
   #define IPS_ERR_SEL_TO               0xF0
   #define IPS_ERR_OU_RUN               0xF2
   #define IPS_ERR_HOST_RESET           0xF7
   #define IPS_ERR_DEV_RESET            0xF8
   #define IPS_ERR_RECOVERY             0xFC
   #define IPS_ERR_CKCOND               0xFF

   /*
    * Operating System Defines
    */
   #define IPS_OS_WINDOWS_NT            0x01
   #define IPS_OS_NETWARE               0x02
   #define IPS_OS_OPENSERVER            0x03
   #define IPS_OS_UNIXWARE              0x04
   #define IPS_OS_SOLARIS               0x05
   #define IPS_OS_OS2                   0x06
   #define IPS_OS_LINUX                 0x07
   #define IPS_OS_FREEBSD               0x08

   /*
    * Adapter Revision ID's
    */
   #define IPS_REVID_SERVERAID          0x02
   #define IPS_REVID_NAVAJO             0x03
   #define IPS_REVID_SERVERAID2         0x04
   #define IPS_REVID_CLARINETP1         0x05
   #define IPS_REVID_CLARINETP2         0x07
   #define IPS_REVID_CLARINETP3         0x0D
   #define IPS_REVID_TROMBONE32         0x0F
   #define IPS_REVID_TROMBONE64         0x10

   /*
    * NVRAM Page 5 Adapter Defines
    */
   #define IPS_ADTYPE_SERVERAID         0x01
   #define IPS_ADTYPE_SERVERAID2        0x02
   #define IPS_ADTYPE_NAVAJO            0x03
   #define IPS_ADTYPE_KIOWA             0x04
   #define IPS_ADTYPE_SERVERAID3        0x05
   #define IPS_ADTYPE_SERVERAID3L       0x06
   #define IPS_ADTYPE_SERVERAID4H       0x07
   #define IPS_ADTYPE_SERVERAID4M       0x08
   #define IPS_ADTYPE_SERVERAID4L       0x09
   #define IPS_ADTYPE_SERVERAID4MX      0x0A
   #define IPS_ADTYPE_SERVERAID4LX      0x0B

   /*
    * Adapter Command/Status Packet Definitions
    */
   #define IPS_SUCCESS                  0x01 /* Successfully completed       */
   #define IPS_SUCCESS_IMM              0x02 /* Success - Immediately        */
   #define IPS_FAILURE                  0x04 /* Completed with Error         */

   /*
    * Logical Drive Equates
    */
   #define IPS_LD_OFFLINE               0x02
   #define IPS_LD_OKAY                  0x03
   #define IPS_LD_FREE                  0x00
   #define IPS_LD_SYS                   0x06
   #define IPS_LD_CRS                   0x24

   /*
    * DCDB Table Equates
    */
   #define IPS_NO_DISCONNECT            0x00
   #define IPS_DISCONNECT_ALLOWED       0x80
   #define IPS_NO_AUTO_REQSEN           0x40
   #define IPS_DATA_NONE                0x00
   #define IPS_DATA_UNK                 0x00
   #define IPS_DATA_IN                  0x01
   #define IPS_DATA_OUT                 0x02
   #define IPS_TRANSFER64K              0x08
   #define IPS_NOTIMEOUT                0x00
   #define IPS_TIMEOUT10                0x10
   #define IPS_TIMEOUT60                0x20
   #define IPS_TIMEOUT20M               0x30

   /*
    * SCSI Inquiry Data Flags
    */
   #define IPS_SCSI_INQ_TYPE_DASD       0x00
   #define IPS_SCSI_INQ_TYPE_PROCESSOR  0x03
   #define IPS_SCSI_INQ_LU_CONNECTED    0x00
   #define IPS_SCSI_INQ_RD_REV2         0x02
   #define IPS_SCSI_INQ_REV2            0x02
   #define IPS_SCSI_INQ_REV3            0x03
   #define IPS_SCSI_INQ_Address16       0x01
   #define IPS_SCSI_INQ_Address32       0x02
   #define IPS_SCSI_INQ_MedChanger      0x08
   #define IPS_SCSI_INQ_MultiPort       0x10
   #define IPS_SCSI_INQ_EncServ         0x40
   #define IPS_SCSI_INQ_SoftReset       0x01
   #define IPS_SCSI_INQ_CmdQue          0x02
   #define IPS_SCSI_INQ_Linked          0x08
   #define IPS_SCSI_INQ_Sync            0x10
   #define IPS_SCSI_INQ_WBus16          0x20
   #define IPS_SCSI_INQ_WBus32          0x40
   #define IPS_SCSI_INQ_RelAdr          0x80

   /*
    * SCSI Request Sense Data Flags
    */
   #define IPS_SCSI_REQSEN_VALID        0x80
   #define IPS_SCSI_REQSEN_CURRENT_ERR  0x70
   #define IPS_SCSI_REQSEN_NO_SENSE     0x00

   /*
    * SCSI Mode Page Equates
    */
   #define IPS_SCSI_MP3_SoftSector      0x01
   #define IPS_SCSI_MP3_HardSector      0x02
   #define IPS_SCSI_MP3_Removeable      0x04
   #define IPS_SCSI_MP3_AllocateSurface 0x08

   /*
    * Configuration Structure Flags
    */
   #define IPS_CFG_USEROPT_UPDATECOUNT(cfg)   (((cfg)->UserOpt & 0xffff000) >> 16)
   #define IPS_CFG_USEROPT_CONCURSTART(cfg)   (((cfg)->UserOpt & 0xf000) >> 12)
   #define IPS_CFG_USEROPT_STARTUPDELAY(cfg)  (((cfg)->UserOpt & 0xf00) >> 8)
   #define IPS_CFG_USEROPT_REARRANGE(cfg)     ((cfg)->UserOpt & 0x80)
   #define IPS_CFG_USEROPT_CDBOOT(cfg)        ((cfg)->UserOpt & 0x40)
   #define IPS_CFG_USEROPT_CLUSTER(cfg)       ((cfg)->UserOpt & 0x20)

   /*
    * Host adapter Flags (bit numbers)
    */
   #define IPS_IN_INTR                  0
   #define IPS_IN_ABORT                 1
   #define IPS_IN_RESET                 2

   /*
    * SCB Flags
    */
   #define IPS_SCB_ACTIVE               0x00001
   #define IPS_SCB_WAITING              0x00002

   /*
    * Passthru stuff
    */
   #define IPS_COPPUSRCMD              (('C'<<8) | 65)
   #define IPS_COPPIOCCMD              (('C'<<8) | 66)
   #define IPS_NUMCTRLS                (('C'<<8) | 68)
   #define IPS_CTRLINFO                (('C'<<8) | 69)
   #define IPS_FLASHBIOS               (('C'<<8) | 70)

   /* time oriented stuff */
   #define IPS_IS_LEAP_YEAR(y)           (((y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0))) ? 1 : 0)
   #define IPS_NUM_LEAP_YEARS_THROUGH(y) ((y) / 4 - (y) / 100 + (y) / 400)

   #define IPS_SECS_MIN                 60
   #define IPS_SECS_HOUR                3600
   #define IPS_SECS_8HOURS              28800
   #define IPS_SECS_DAY                 86400
   #define IPS_DAYS_NORMAL_YEAR         365
   #define IPS_DAYS_LEAP_YEAR           366
   #define IPS_EPOCH_YEAR               1970

   /*
    * Scsi_Host Template
    */
#if LINUX_VERSION_CODE < LinuxVersionCode(2,3,27)
 #define IPS {                            \
    next : NULL,                          \
    module : NULL,                        \
    proc_info : NULL,                     \
    proc_dir : NULL,                      \
    name : NULL,                          \
    detect : ips_detect,                  \
    release : ips_release,                \
    info : ips_info,                      \
    command : NULL,                       \
    queuecommand : ips_queue,             \
    eh_strategy_handler : NULL,           \
    eh_abort_handler : ips_eh_abort,      \
    eh_device_reset_handler : NULL,       \
    eh_bus_reset_handler : NULL,          \
    eh_host_reset_handler : ips_eh_reset, \
    abort : NULL,                         \
    reset : NULL,                         \
    slave_attach : NULL,                  \
    bios_param : ips_biosparam,           \
    can_queue : 0,                        \
    this_id: -1,                          \
    sg_tablesize : IPS_MAX_SG,            \
    cmd_per_lun: 16,                      \
    present : 0,                          \
    unchecked_isa_dma : 0,                \
    use_clustering : ENABLE_CLUSTERING,   \
    use_new_eh_code : 1                   \
}
#else
 #define IPS {                            \
    next : NULL,                          \
    module : NULL,                        \
    proc_info : NULL,                     \
    name : NULL,                          \
    detect : ips_detect,                  \
    release : ips_release,                \
    info : ips_info,                      \
    command : NULL,                       \
    queuecommand : ips_queue,             \
    eh_strategy_handler : NULL,           \
    eh_abort_handler : ips_eh_abort,      \
    eh_device_reset_handler : NULL,       \
    eh_bus_reset_handler : NULL,          \
    eh_host_reset_handler : ips_eh_reset, \
    abort : NULL,                         \
    reset : NULL,                         \
    slave_attach : NULL,                  \
    bios_param : ips_biosparam,           \
    can_queue : 0,                        \
    this_id: -1,                          \
    sg_tablesize : IPS_MAX_SG,            \
    cmd_per_lun: 16,                      \
    present : 0,                          \
    unchecked_isa_dma : 0,                \
    use_clustering : ENABLE_CLUSTERING,   \
    use_new_eh_code : 1                   \
}
#endif

/*
 * IBM PCI Raid Command Formats
 */
typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int8_t  log_drv;
   u_int8_t  sg_count;
   u_int32_t lba;
   u_int32_t sg_addr;
   u_int16_t sector_count;
   u_int16_t reserved;
   u_int32_t ccsar;
   u_int32_t cccr;
} IPS_IO_CMD, *PIPS_IO_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int16_t reserved;
   u_int32_t reserved2;
   u_int32_t buffer_addr;
   u_int32_t reserved3;
   u_int32_t ccsar;
   u_int32_t cccr;
} IPS_LD_CMD, *PIPS_LD_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int8_t  reserved;
   u_int8_t  reserved2;
   u_int32_t reserved3;
   u_int32_t buffer_addr;
   u_int32_t reserved4;
} IPS_IOCTL_CMD, *PIPS_IOCTL_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int16_t reserved;
   u_int32_t reserved2;
   u_int32_t dcdb_address;
   u_int32_t reserved3;
   u_int32_t ccsar;
   u_int32_t cccr;
} IPS_DCDB_CMD, *PIPS_DCDB_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int8_t  channel;
   u_int8_t  source_target;
   u_int32_t reserved;
   u_int32_t reserved2;
   u_int32_t reserved3;
   u_int32_t ccsar;
   u_int32_t cccr;
} IPS_CS_CMD, *PIPS_CS_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int8_t  log_drv;
   u_int8_t  control;
   u_int32_t reserved;
   u_int32_t reserved2;
   u_int32_t reserved3;
   u_int32_t ccsar;
   u_int32_t cccr;
} IPS_US_CMD, *PIPS_US_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int8_t  reserved;
   u_int8_t  state;
   u_int32_t reserved2;
   u_int32_t reserved3;
   u_int32_t reserved4;
   u_int32_t ccsar;
   u_int32_t cccr;
} IPS_FC_CMD, *PIPS_FC_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int8_t  reserved;
   u_int8_t  desc;
   u_int32_t reserved2;
   u_int32_t buffer_addr;
   u_int32_t reserved3;
   u_int32_t ccsar;
   u_int32_t cccr;
} IPS_STATUS_CMD, *PIPS_STATUS_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int8_t  page;
   u_int8_t  write;
   u_int32_t reserved;
   u_int32_t buffer_addr;
   u_int32_t reserved2;
   u_int32_t ccsar;
   u_int32_t cccr;
} IPS_NVRAM_CMD, *PIPS_NVRAM_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int8_t  reset_count;
   u_int8_t  reset_type;
   u_int8_t  second;
   u_int8_t  minute;
   u_int8_t  hour;
   u_int8_t  day;
   u_int8_t  reserved1[4];
   u_int8_t  month;
   u_int8_t  yearH;
   u_int8_t  yearL;
   u_int8_t  reserved2;
} IPS_FFDC_CMD, *PIPS_FFDC_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int8_t  type;
   u_int8_t  direction;
   u_int32_t count;
   u_int32_t buffer_addr;
   u_int8_t  total_packets;
   u_int8_t  packet_num;
   u_int16_t reserved;
} IPS_FLASHFW_CMD, *PIPS_FLASHFW_CMD;

typedef struct {
   u_int8_t  op_code;
   u_int8_t  command_id;
   u_int8_t  type;
   u_int8_t  direction;
   u_int32_t count;
   u_int32_t buffer_addr;
   u_int32_t offset;
} IPS_FLASHBIOS_CMD, *PIPS_FLASHBIOS_CMD;

typedef union {
   IPS_IO_CMD        basic_io;
   IPS_LD_CMD        logical_info;
   IPS_IOCTL_CMD     ioctl_info;
   IPS_DCDB_CMD      dcdb;
   IPS_CS_CMD        config_sync;
   IPS_US_CMD        unlock_stripe;
   IPS_FC_CMD        flush_cache;
   IPS_STATUS_CMD    status;
   IPS_NVRAM_CMD     nvram;
   IPS_FFDC_CMD      ffdc;
   IPS_FLASHFW_CMD    flashfw;
   IPS_FLASHBIOS_CMD  flashbios;
} IPS_HOST_COMMAND, *PIPS_HOST_COMMAND;

typedef struct {
   u_int8_t  logical_id;
   u_int8_t  reserved;
   u_int8_t  raid_level;
   u_int8_t  state;
   u_int32_t sector_count;
} IPS_DRIVE_INFO, *PIPS_DRIVE_INFO;

typedef struct {
   u_int8_t       no_of_log_drive;
   u_int8_t       reserved[3];
   IPS_DRIVE_INFO drive_info[IPS_MAX_LD];
} IPS_LD_INFO, *PIPS_LD_INFO;

typedef struct {
   u_int8_t   device_address;
   u_int8_t   cmd_attribute;
   u_int16_t  transfer_length;
   u_int32_t  buffer_pointer;
   u_int8_t   cdb_length;
   u_int8_t   sense_length;
   u_int8_t   sg_count;
   u_int8_t   reserved;
   u_int8_t   scsi_cdb[12];
   u_int8_t   sense_info[64];
   u_int8_t   scsi_status;
   u_int8_t   reserved2[3];
} IPS_DCDB_TABLE, *PIPS_DCDB_TABLE;

typedef union {
   struct {
      volatile u_int8_t  reserved;
      volatile u_int8_t  command_id;
      volatile u_int8_t  basic_status;
      volatile u_int8_t  extended_status;
   } fields;

   volatile u_int32_t    value;
} IPS_STATUS, *PIPS_STATUS;

typedef struct {
   IPS_STATUS           status[IPS_MAX_CMDS + 1];
   volatile PIPS_STATUS p_status_start;
   volatile PIPS_STATUS p_status_end;
   volatile PIPS_STATUS p_status_tail;
   volatile u_int32_t   hw_status_start;
   volatile u_int32_t   hw_status_tail;
   IPS_LD_INFO          logical_drive_info;
} IPS_ADAPTER, *PIPS_ADAPTER;

typedef struct {
   u_int8_t  ucLogDriveCount;
   u_int8_t  ucMiscFlag;
   u_int8_t  ucSLTFlag;
   u_int8_t  ucBSTFlag;
   u_int8_t  ucPwrChgCnt;
   u_int8_t  ucWrongAdrCnt;
   u_int8_t  ucUnidentCnt;
   u_int8_t  ucNVramDevChgCnt;
   u_int8_t  CodeBlkVersion[8];
   u_int8_t  BootBlkVersion[8];
   u_int32_t ulDriveSize[IPS_MAX_LD];
   u_int8_t  ucConcurrentCmdCount;
   u_int8_t  ucMaxPhysicalDevices;
   u_int16_t usFlashRepgmCount;
   u_int8_t  ucDefunctDiskCount;
   u_int8_t  ucRebuildFlag;
   u_int8_t  ucOfflineLogDrvCount;
   u_int8_t  ucCriticalDrvCount;
   u_int16_t usConfigUpdateCount;
   u_int8_t  ucBlkFlag;
   u_int8_t  reserved;
   u_int16_t usAddrDeadDisk[IPS_MAX_CHANNELS * IPS_MAX_TARGETS];
} IPS_ENQ, *PIPS_ENQ;

typedef struct {
   u_int8_t  ucInitiator;
   u_int8_t  ucParameters;
   u_int8_t  ucMiscFlag;
   u_int8_t  ucState;
   u_int32_t ulBlockCount;
   u_int8_t  ucDeviceId[28];
} IPS_DEVSTATE, *PIPS_DEVSTATE;

typedef struct {
   u_int8_t  ucChn;
   u_int8_t  ucTgt;
   u_int16_t ucReserved;
   u_int32_t ulStartSect;
   u_int32_t ulNoOfSects;
} IPS_CHUNK, *PIPS_CHUNK;

typedef struct {
   u_int16_t ucUserField;
   u_int8_t  ucState;
   u_int8_t  ucRaidCacheParam;
   u_int8_t  ucNoOfChunkUnits;
   u_int8_t  ucStripeSize;
   u_int8_t  ucParams;
   u_int8_t  ucReserved;
   u_int32_t ulLogDrvSize;
   IPS_CHUNK chunk[IPS_MAX_CHUNKS];
} IPS_LD, *PIPS_LD;

typedef struct {
   u_int8_t  board_disc[8];
   u_int8_t  processor[8];
   u_int8_t  ucNoChanType;
   u_int8_t  ucNoHostIntType;
   u_int8_t  ucCompression;
   u_int8_t  ucNvramType;
   u_int32_t ulNvramSize;
} IPS_HARDWARE, *PIPS_HARDWARE;

typedef struct {
   u_int8_t       ucLogDriveCount;
   u_int8_t       ucDateD;
   u_int8_t       ucDateM;
   u_int8_t       ucDateY;
   u_int8_t       init_id[4];
   u_int8_t       host_id[12];
   u_int8_t       time_sign[8];
   u_int32_t      UserOpt;
   u_int16_t      user_field;
   u_int8_t       ucRebuildRate;
   u_int8_t       ucReserve;
   IPS_HARDWARE   hardware_disc;
   IPS_LD         logical_drive[IPS_MAX_LD];
   IPS_DEVSTATE   dev[IPS_MAX_CHANNELS][IPS_MAX_TARGETS+1];
   u_int8_t       reserved[512];
} IPS_CONF, *PIPS_CONF;

typedef struct {
   u_int32_t  signature;
   u_int8_t   reserved;
   u_int8_t   adapter_slot;
   u_int16_t  adapter_type;
   u_int8_t   bios_high[4];
   u_int8_t   bios_low[4];
   u_int16_t  reserved2;
   u_int8_t   reserved3;
   u_int8_t   operating_system;
   u_int8_t   driver_high[4];
   u_int8_t   driver_low[4];
   u_int8_t   reserved4[100];
} IPS_NVRAM_P5, *PIPS_NVRAM_P5;

typedef struct _IPS_SUBSYS {
   u_int32_t  param[128];
} IPS_SUBSYS, *PIPS_SUBSYS;

/**
 ** SCSI Structures
 **/

/*
 * Inquiry Data Format
 */
typedef struct {
   u_int8_t  DeviceType;
   u_int8_t  DeviceTypeQualifier;
   u_int8_t  Version;
   u_int8_t  ResponseDataFormat;
   u_int8_t  AdditionalLength;
   u_int8_t  Reserved;
   u_int8_t  Flags[2];
   char      VendorId[8];
   char      ProductId[16];
   char      ProductRevisionLevel[4];
} IPS_SCSI_INQ_DATA, *PIPS_SCSI_INQ_DATA;

/*
 * Read Capacity Data Format
 */
typedef struct {
   u_int32_t lba;
   u_int32_t len;
} IPS_SCSI_CAPACITY;

/*
 * Request Sense Data Format
 */
typedef struct {
   u_int8_t  ResponseCode;
   u_int8_t  SegmentNumber;
   u_int8_t  Flags;
   u_int8_t  Information[4];
   u_int8_t  AdditionalLength;
   u_int8_t  CommandSpecific[4];
   u_int8_t  AdditionalSenseCode;
   u_int8_t  AdditionalSenseCodeQual;
   u_int8_t  FRUCode;
   u_int8_t  SenseKeySpecific[3];
} IPS_SCSI_REQSEN;

/*
 * Sense Data Format - Page 3
 */
typedef struct {
   u_int8_t  PageCode;
   u_int8_t  PageLength;
   u_int16_t TracksPerZone;
   u_int16_t AltSectorsPerZone;
   u_int16_t AltTracksPerZone;
   u_int16_t AltTracksPerVolume;
   u_int16_t SectorsPerTrack;
   u_int16_t BytesPerSector;
   u_int16_t Interleave;
   u_int16_t TrackSkew;
   u_int16_t CylinderSkew;
   u_int8_t  flags;
   u_int8_t  reserved[3];
} IPS_SCSI_MODE_PAGE3;

/*
 * Sense Data Format - Page 4
 */
typedef struct {
   u_int8_t  PageCode;
   u_int8_t  PageLength;
   u_int16_t CylindersHigh;
   u_int8_t  CylindersLow;
   u_int8_t  Heads;
   u_int16_t WritePrecompHigh;
   u_int8_t  WritePrecompLow;
   u_int16_t ReducedWriteCurrentHigh;
   u_int8_t  ReducedWriteCurrentLow;
   u_int16_t StepRate;
   u_int16_t LandingZoneHigh;
   u_int8_t  LandingZoneLow;
   u_int8_t  flags;
   u_int8_t  RotationalOffset;
   u_int8_t  Reserved;
   u_int16_t MediumRotationRate;
   u_int8_t  Reserved2[2];
} IPS_SCSI_MODE_PAGE4;

/*
 * Sense Data Format - Block Descriptor (DASD)
 */
typedef struct {
   u_int32_t NumberOfBlocks;
   u_int8_t  DensityCode;
   u_int16_t BlockLengthHigh;
   u_int8_t  BlockLengthLow;
} IPS_SCSI_MODE_PAGE_BLKDESC;

/*
 * Sense Data Format - Mode Page Header
 */
typedef struct {
   u_int8_t  DataLength;
   u_int8_t  MediumType;
   u_int8_t  Reserved;
   u_int8_t  BlockDescLength;
} IPS_SCSI_MODE_PAGE_HEADER;

typedef struct {
   IPS_SCSI_MODE_PAGE_HEADER  hdr;
   IPS_SCSI_MODE_PAGE_BLKDESC blkdesc;

   union {
      IPS_SCSI_MODE_PAGE3 pg3;
      IPS_SCSI_MODE_PAGE4 pg4;
   } pdata;
} IPS_SCSI_MODE_PAGE_DATA;

/*
 * Scatter Gather list format
 */
typedef struct ips_sglist {
   u_int32_t address;
   u_int32_t length;
} IPS_SG_LIST, *PIPS_SG_LIST;

typedef struct _IPS_INFOSTR {
   char *buffer;
   int   length;
   int   offset;
   int   pos;
   int   localpos;
} IPS_INFOSTR;

typedef struct {
   char *option_name;
   int  *option_flag;
   int   option_value;
} IPS_OPTION;

typedef struct {
   void             *userbuffer;
   u_int32_t         usersize;
   void             *kernbuffer;
   u_int32_t         kernsize;
   void             *ha;
   void             *SC;
   void             *pt;
   struct semaphore *sem;
   u_int32_t         offset;
   u_int32_t         retcode;
} IPS_FLASH_DATA;

/*
 * Status Info
 */
typedef struct ips_stat {
   u_int32_t residue_len;
   void     *scb_addr;
   u_int8_t  padding[12 - sizeof(void *)];
} ips_stat_t;

/*
 * SCB Queue Format
 */
typedef struct ips_scb_queue {
   struct ips_scb *head;
   struct ips_scb *tail;
   u_int32_t       count;
   u_int32_t       cpu_flags;
   spinlock_t      lock;
} ips_scb_queue_t;

/*
 * Wait queue_format
 */
typedef struct ips_wait_queue {
   Scsi_Cmnd      *head;
   Scsi_Cmnd      *tail;
   u_int32_t       count;
   u_int32_t       cpu_flags;
   spinlock_t      lock;
} ips_wait_queue_t;

typedef struct ips_copp_wait_item {
   Scsi_Cmnd                 *scsi_cmd;
   struct semaphore          *sem;
   struct ips_copp_wait_item *next;
} ips_copp_wait_item_t;

typedef struct ips_copp_queue {
   struct ips_copp_wait_item *head;
   struct ips_copp_wait_item *tail;
   u_int32_t                  count;
   u_int32_t                  cpu_flags;
   spinlock_t                 lock;
} ips_copp_queue_t;

/* forward decl for host structure */
struct ips_ha;

typedef struct {
   int       (*reset)(struct ips_ha *);
   int       (*issue)(struct ips_ha *, struct ips_scb *);
   int       (*isinit)(struct ips_ha *);
   int       (*isintr)(struct ips_ha *);
   int       (*init)(struct ips_ha *);
   int       (*erasebios)(struct ips_ha *);
   int       (*programbios)(struct ips_ha *, char *, u_int32_t, u_int32_t);
   int       (*verifybios)(struct ips_ha *, char *, u_int32_t, u_int32_t);
   void      (*statinit)(struct ips_ha *);
   void      (*intr)(struct ips_ha *);
   void      (*enableint)(struct ips_ha *);
   u_int32_t (*statupd)(struct ips_ha *);
} ips_hw_func_t;

typedef struct ips_ha {
   u_int8_t           ha_id[IPS_MAX_CHANNELS+1];
   u_int32_t          dcdb_active[IPS_MAX_CHANNELS];
   u_int32_t          io_addr;            /* Base I/O address           */
   u_int8_t           irq;                /* IRQ for adapter            */
   u_int8_t           ntargets;           /* Number of targets          */
   u_int8_t           nbus;               /* Number of buses            */
   u_int8_t           nlun;               /* Number of Luns             */
   u_int16_t          ad_type;            /* Adapter type               */
   u_int16_t          host_num;           /* Adapter number             */
   u_int32_t          max_xfer;           /* Maximum Xfer size          */
   u_int32_t          max_cmds;           /* Max concurrent commands    */
   u_int32_t          num_ioctl;          /* Number of Ioctls           */
   ips_stat_t         sp;                 /* Status packer pointer      */
   struct ips_scb    *scbs;               /* Array of all CCBS          */
   struct ips_scb    *scb_freelist;       /* SCB free list              */
   ips_wait_queue_t   scb_waitlist;       /* Pending SCB list           */
   ips_copp_queue_t   copp_waitlist;      /* Pending PT list            */
   ips_scb_queue_t    scb_activelist;     /* Active SCB list            */
   IPS_IO_CMD        *dummy;              /* dummy command              */
   IPS_ADAPTER       *adapt;              /* Adapter status area        */
   IPS_ENQ           *enq;                /* Adapter Enquiry data       */
   IPS_CONF          *conf;               /* Adapter config data        */
   IPS_NVRAM_P5      *nvram;              /* NVRAM page 5 data          */
   IPS_SUBSYS        *subsys;             /* Subsystem parameters       */
   char              *ioctl_data;         /* IOCTL data area            */
   u_int32_t          ioctl_datasize;     /* IOCTL data size            */
   u_int32_t          cmd_in_progress;    /* Current command in progress*/
   unsigned long      flags;              /* HA flags                   */
   u_int8_t           waitflag;           /* are we waiting for cmd     */
   u_int8_t           active;
   u_int16_t          reset_count;        /* number of resets           */
   u_int32_t          last_ffdc;          /* last time we sent ffdc info*/
   u_int8_t           revision_id;        /* Revision level             */
   u_int16_t          device_id;          /* PCI device ID              */
   u_int8_t           slot_num;           /* PCI Slot Number            */
   u_int16_t          subdevice_id;       /* Subsystem device ID        */
   u_int8_t           ioctl_order;        /* Number of pages in ioctl   */
   u_int8_t           reserved2;          /* Empty                      */
   u_int8_t           bios_version[8];    /* BIOS Revision              */
   u_int32_t          mem_addr;           /* Memory mapped address      */
   u_int32_t          io_len;             /* Size of IO Address         */
   u_int32_t          mem_len;            /* Size of memory address     */
   char              *mem_ptr;            /* Memory mapped Ptr          */
   char              *ioremap_ptr;        /* ioremapped memory pointer  */
   ips_hw_func_t      func;               /* hw function pointers       */
   struct pci_dev    *pcidev;             /* PCI device handle          */
   spinlock_t         scb_lock;
   spinlock_t         copp_lock;
   spinlock_t         ips_lock;
   struct semaphore   ioctl_sem;          /* Semaphore for new IOCTL's  */
   struct semaphore   flash_ioctl_sem;    /* Semaphore for Flashing     */
   char              *save_ioctl_data;    /* Save Area for ioctl_data   */
   u8                 save_ioctl_order;   /* Save Area for ioctl_order  */
   u32                save_ioctl_datasize;/* Save Area for ioctl_datasize */
} ips_ha_t;

typedef void (*ips_scb_callback) (ips_ha_t *, struct ips_scb *);

/*
 * SCB Format
 */
typedef struct ips_scb {
   IPS_HOST_COMMAND  cmd;
   IPS_DCDB_TABLE    dcdb;
   u_int8_t          target_id;
   u_int8_t          bus;
   u_int8_t          lun;
   u_int8_t          cdb[12];
   u_int32_t         scb_busaddr;
   u_int32_t         data_busaddr;
   u_int32_t         timeout;
   u_int8_t          basic_status;
   u_int8_t          extended_status;
   u_int8_t          breakup;
   u_int8_t          sg_break;
   u_int32_t         data_len;
   u_int32_t         sg_len;
   u_int32_t         flags;
   u_int32_t         op_code;
   IPS_SG_LIST      *sg_list;
   Scsi_Cmnd        *scsi_cmd;
   struct ips_scb   *q_next;
   ips_scb_callback  callback;
   struct semaphore *sem;
} ips_scb_t;

typedef struct ips_scb_pt {
   IPS_HOST_COMMAND  cmd;
   IPS_DCDB_TABLE    dcdb;
   u_int8_t          target_id;
   u_int8_t          bus;
   u_int8_t          lun;
   u_int8_t          cdb[12];
   u_int32_t         scb_busaddr;
   u_int32_t         data_busaddr;
   u_int32_t         timeout;
   u_int8_t          basic_status;
   u_int8_t          extended_status;
   u_int16_t         breakup;
   u_int32_t         data_len;
   u_int32_t         sg_len;
   u_int32_t         flags;
   u_int32_t         op_code;
   IPS_SG_LIST      *sg_list;
   Scsi_Cmnd        *scsi_cmd;
   struct ips_scb   *q_next;
   ips_scb_callback  callback;
} ips_scb_pt_t;

/*
 * Passthru Command Format
 */
typedef struct {
   u_int8_t      CoppID[4];
   u_int32_t     CoppCmd;
   u_int32_t     PtBuffer;
   u_int8_t     *CmdBuffer;
   u_int32_t     CmdBSize;
   ips_scb_pt_t  CoppCP;
   u_int32_t     TimeOut;
   u_int8_t      BasicStatus;
   u_int8_t      ExtendedStatus;
   u_int16_t     reserved;
} ips_passthru_t;

#endif

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

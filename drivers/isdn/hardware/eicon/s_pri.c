
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include "platform.h"
#include "di_defs.h"
#include "pc.h"
#include "pr_pc.h"
#include "di.h"
#include "mi_pc.h"
#include "pc_maint.h"
#include "divasync.h"
#include "io.h"
#include "helpers.h"
#include "dsrv_pri.h"
#include "dsp_defs.h"
/*****************************************************************************/
#define MAX_XLOG_SIZE  (64 * 1024)
/* -------------------------------------------------------------------------
  Does return offset between ADAPTER->ram and real begin of memory
  ------------------------------------------------------------------------- */
static dword pri_ram_offset (ADAPTER* a) {
 return ((dword)MP_SHARED_RAM_OFFSET);
}
/* -------------------------------------------------------------------------
  Recovery XLOG buffer from the card
  ------------------------------------------------------------------------- */
static void pri_cpu_trapped (PISDN_ADAPTER IoAdapter) {
 byte  __iomem *base ;
 word *Xlog ;
 dword   regs[4], TrapID, size ;
 Xdesc   xlogDesc ;
/*
 * check for trapped MIPS 46xx CPU, dump exception frame
 */
 base   = DIVA_OS_MEM_ATTACH_ADDRESS(IoAdapter);
 TrapID = READ_DWORD(&base[0x80]) ;
 if ( (TrapID == 0x99999999) || (TrapID == 0x99999901) )
 {
  dump_trap_frame (IoAdapter, &base[0x90]) ;
  IoAdapter->trapped = 1 ;
 }
 regs[0] = READ_DWORD(&base[MP_PROTOCOL_OFFSET + 0x70]);
 regs[1] = READ_DWORD(&base[MP_PROTOCOL_OFFSET + 0x74]);
 regs[2] = READ_DWORD(&base[MP_PROTOCOL_OFFSET + 0x78]);
 regs[3] = READ_DWORD(&base[MP_PROTOCOL_OFFSET + 0x7c]);
 regs[0] &= IoAdapter->MemorySize - 1 ;
 if ( (regs[0] < IoAdapter->MemorySize - 1) )
 {
  if ( !(Xlog = (word *)diva_os_malloc (0, MAX_XLOG_SIZE)) ) {
   DIVA_OS_MEM_DETACH_ADDRESS(IoAdapter, base);
   return ;
  }
  size = IoAdapter->MemorySize - regs[0] ;
  if ( size > MAX_XLOG_SIZE )
   size = MAX_XLOG_SIZE ;
  memcpy_fromio(Xlog, &base[regs[0]], size) ;
  xlogDesc.buf = Xlog ;
  xlogDesc.cnt = READ_WORD(&base[regs[1] & (IoAdapter->MemorySize - 1)]) ;
  xlogDesc.out = READ_WORD(&base[regs[2] & (IoAdapter->MemorySize - 1)]) ;
  dump_xlog_buffer (IoAdapter, &xlogDesc) ;
  diva_os_free (0, Xlog) ;
  IoAdapter->trapped = 2 ;
 }
 DIVA_OS_MEM_DETACH_ADDRESS(IoAdapter, base);
}
/* -------------------------------------------------------------------------
  Hardware reset of PRI card
  ------------------------------------------------------------------------- */
static void reset_pri_hardware (PISDN_ADAPTER IoAdapter) {
 byte __iomem *p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
 WRITE_BYTE(p, _MP_RISC_RESET | _MP_LED1 | _MP_LED2);
 diva_os_wait (50) ;
 WRITE_BYTE(p, 0x00);
 diva_os_wait (50) ;
 DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
}
/* -------------------------------------------------------------------------
  Stop Card Hardware
  ------------------------------------------------------------------------- */
static void stop_pri_hardware (PISDN_ADAPTER IoAdapter) {
 dword i;
 byte __iomem *p;
 dword volatile __iomem *cfgReg = (void __iomem *)DIVA_OS_MEM_ATTACH_CFG(IoAdapter);
 WRITE_DWORD(&cfgReg[3], 0);
 WRITE_DWORD(&cfgReg[1], 0);
 DIVA_OS_MEM_DETACH_CFG(IoAdapter, cfgReg);
 IoAdapter->a.ram_out (&IoAdapter->a, &RAM->SWReg, SWREG_HALT_CPU) ;
 i = 0 ;
 while ( (i < 100) && (IoAdapter->a.ram_in (&IoAdapter->a, &RAM->SWReg) != 0) )
 {
  diva_os_wait (1) ;
  i++ ;
 }
 DBG_TRC(("%s: PRI stopped (%d)", IoAdapter->Name, i))
 cfgReg = (void __iomem *)DIVA_OS_MEM_ATTACH_CFG(IoAdapter);
 WRITE_DWORD(&cfgReg[0],((dword)(~0x03E00000)));
 DIVA_OS_MEM_DETACH_CFG(IoAdapter, cfgReg);
 diva_os_wait (1) ;
 p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
 WRITE_BYTE(p, _MP_RISC_RESET | _MP_LED1 | _MP_LED2);
 DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
}
#if !defined(DIVA_USER_MODE_CARD_CONFIG) /* { */
/* -------------------------------------------------------------------------
  Load protocol code to the PRI Card
  ------------------------------------------------------------------------- */
#define DOWNLOAD_ADDR(IoAdapter) (IoAdapter->downloadAddr & (IoAdapter->MemorySize - 1))
static int pri_protocol_load (PISDN_ADAPTER IoAdapter) {
 dword  FileLength ;
 dword *File ;
 dword *sharedRam ;
 dword  Addr ;
 byte *p;
 if (!(File = (dword *)xdiLoadArchive (IoAdapter, &FileLength, 0))) {
  return (0) ;
 }
 IoAdapter->features = diva_get_protocol_file_features ((byte*)File,
                                        OFFS_PROTOCOL_ID_STRING,
                                        IoAdapter->ProtocolIdString,
                                        sizeof(IoAdapter->ProtocolIdString)) ;
 IoAdapter->a.protocol_capabilities = IoAdapter->features ;
 DBG_LOG(("Loading %s", IoAdapter->ProtocolIdString))
 Addr = ((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR]))
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 1])) << 8)
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 2])) << 16)
   | (((dword)(((byte *) File)[OFFS_PROTOCOL_END_ADDR + 3])) << 24) ;
        if ( Addr != 0 )
 {
  IoAdapter->DspCodeBaseAddr = (Addr + 3) & (~3) ;
  IoAdapter->MaxDspCodeSize = (MP_UNCACHED_ADDR (IoAdapter->MemorySize)
                            - IoAdapter->DspCodeBaseAddr) & (IoAdapter->MemorySize - 1) ;
  Addr = IoAdapter->DspCodeBaseAddr ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR] = (byte) Addr ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR + 1] = (byte)(Addr >> 8) ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR + 2] = (byte)(Addr >> 16) ;
  ((byte *) File)[OFFS_DSP_CODE_BASE_ADDR + 3] = (byte)(Addr >> 24) ;
  IoAdapter->InitialDspInfo = 0x80 ;
 }
 else
 {
  if ( IoAdapter->features & PROTCAP_VOIP )
   IoAdapter->MaxDspCodeSize = MP_VOIP_MAX_DSP_CODE_SIZE ;
  else if ( IoAdapter->features & PROTCAP_V90D )
   IoAdapter->MaxDspCodeSize = MP_V90D_MAX_DSP_CODE_SIZE ;
  else
   IoAdapter->MaxDspCodeSize = MP_ORG_MAX_DSP_CODE_SIZE ;
  IoAdapter->DspCodeBaseAddr = MP_CACHED_ADDR (IoAdapter->MemorySize -
                                               IoAdapter->MaxDspCodeSize) ;
  IoAdapter->InitialDspInfo = (IoAdapter->MaxDspCodeSize
                            - MP_ORG_MAX_DSP_CODE_SIZE) >> 14 ;
 }
 DBG_LOG(("DSP code base 0x%08lx, max size 0x%08lx (%08lx,%02x)",
          IoAdapter->DspCodeBaseAddr, IoAdapter->MaxDspCodeSize,
          Addr, IoAdapter->InitialDspInfo))
 if ( FileLength > ((IoAdapter->DspCodeBaseAddr -
                     MP_CACHED_ADDR (MP_PROTOCOL_OFFSET)) & (IoAdapter->MemorySize - 1)) )
 {
  xdiFreeFile (File);
  DBG_FTL(("Protocol code '%s' too long (%ld)",
           &IoAdapter->Protocol[0], FileLength))
  return (0) ;
 }
 IoAdapter->downloadAddr = MP_UNCACHED_ADDR (MP_PROTOCOL_OFFSET) ;
 p = DIVA_OS_MEM_ATTACH_RAM(IoAdapter);
 sharedRam = (dword *)(&p[DOWNLOAD_ADDR(IoAdapter)]);
 memcpy (sharedRam, File, FileLength) ;
 if ( memcmp (sharedRam, File, FileLength) )
 {
  DIVA_OS_MEM_DETACH_RAM(IoAdapter, p);
  DBG_FTL(("%s: Memory test failed!", IoAdapter->Properties.Name))
  xdiFreeFile (File);
  return (0) ;
 }
 DIVA_OS_MEM_DETACH_RAM(IoAdapter, p);
 xdiFreeFile (File);
 return (1) ;
}
/******************************************************************************/
/*------------------------------------------------------------------
  Dsp related definitions
  ------------------------------------------------------------------ */
#define DSP_SIGNATURE_PROBE_WORD 0x5a5a
/*
**  Checks presence of DSP on board
*/
static int
dsp_check_presence (volatile byte* addr, volatile byte* data, int dsp)
{
  word pattern;
  *(volatile word*)addr = 0x4000;
  *(volatile word*)data = DSP_SIGNATURE_PROBE_WORD;
  *(volatile word*)addr = 0x4000;
  pattern = *(volatile word*)data;
  if (pattern != DSP_SIGNATURE_PROBE_WORD) {
    DBG_TRC(("W: DSP[%d] %04x(is) != %04x(should)",
              dsp, pattern, DSP_SIGNATURE_PROBE_WORD))
    return (-1);
  }
  *(volatile word*)addr = 0x4000;
  *(volatile word*)data = ~DSP_SIGNATURE_PROBE_WORD;
  *(volatile word*)addr = 0x4000;
  pattern = *(volatile word*)data;
  if (pattern != (word)~DSP_SIGNATURE_PROBE_WORD) {
    DBG_ERR(("A: DSP[%d] %04x(is) != %04x(should)",
              dsp, pattern, (word)~DSP_SIGNATURE_PROBE_WORD))
    return (-2);
  }
  DBG_TRC (("DSP[%d] present", dsp))
  return (0);
}
/*
**  Check if DSP's are present and operating
**  Information about detected DSP's is returned as bit mask
**  Bit 0  - DSP1
**  ...
**  ...
**  ...
**  Bit 29 - DSP30
*/
static dword
diva_pri_detect_dsps (PISDN_ADAPTER IoAdapter)
{
  byte* base;
  byte* p;
  dword ret = 0, DspCount = 0 ;
  dword row_offset[] = {
    0x00000000,
    0x00000800, /* 1 - ROW 1 */
    0x00000840, /* 2 - ROW 2 */
    0x00001000, /* 3 - ROW 3 */
    0x00001040, /* 4 - ROW 4 */
    0x00000000  /* 5 - ROW 0 */
  };
  byte *dsp_addr_port, *dsp_data_port, row_state;
  int dsp_row = 0, dsp_index, dsp_num;
 IoAdapter->InitialDspInfo &= 0xffff ;
 p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
  if (!p)
  {
    DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
    return (0);
  }
  *(volatile byte*)(p) = _MP_RISC_RESET | _MP_DSP_RESET;
  DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
  diva_os_wait (5) ;

  base = DIVA_OS_MEM_ATTACH_CONTROL(IoAdapter);
  
  for (dsp_num = 0; dsp_num < 30; dsp_num++) {
    dsp_row   = dsp_num / 7 + 1;
    dsp_index = dsp_num % 7;
    dsp_data_port = base;
    dsp_addr_port = base;
    dsp_data_port += row_offset[dsp_row];
    dsp_addr_port += row_offset[dsp_row];
    dsp_data_port += (dsp_index * 8);
    dsp_addr_port += (dsp_index * 8) + 0x80;
    if (!dsp_check_presence (dsp_addr_port, dsp_data_port, dsp_num+1)) {
      ret |= (1 << dsp_num);
   DspCount++ ;
    }
  }
  DIVA_OS_MEM_DETACH_CONTROL(IoAdapter, base);

  p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
  *(volatile byte*)(p) = _MP_RISC_RESET | _MP_LED1 | _MP_LED2;
  diva_os_wait (50) ;
  /*
    Verify modules
  */
  for (dsp_row = 0; dsp_row < 4; dsp_row++) {
    row_state = (byte)((ret >> (dsp_row*7)) & 0x7F);
    if (row_state && (row_state != 0x7F)) {
      for (dsp_index = 0; dsp_index < 7; dsp_index++) {
        if (!(row_state & (1 << dsp_index))) {
          DBG_ERR (("A: MODULE[%d]-DSP[%d] failed", dsp_row+1, dsp_index+1))
        }
      }
    }
  }
  if (!(ret & 0x10000000)) {
    DBG_ERR (("A: ON BOARD-DSP[1] failed"))
  }
  if (!(ret & 0x20000000)) {
    DBG_ERR (("A: ON BOARD-DSP[2] failed"))
  }
  /*
    Print module population now
  */
  DBG_LOG(("+-----------------------+"))
  DBG_LOG(("| DSP MODULE POPULATION |"))
  DBG_LOG(("+-----------------------+"))
  DBG_LOG(("|  1  |  2  |  3  |  4  |"))
  DBG_LOG(("+-----------------------+"))
  DBG_LOG(("|  %s  |  %s  |  %s  |  %s  |",
    ((ret >> (0*7)) & 0x7F) ? "Y" : "N",
    ((ret >> (1*7)) & 0x7F) ? "Y" : "N",
    ((ret >> (2*7)) & 0x7F) ? "Y" : "N",
    ((ret >> (3*7)) & 0x7F) ? "Y" : "N"))
  DBG_LOG(("+-----------------------+"))
  DBG_LOG(("DSP's(present-absent):%08x-%08x", ret, ~ret & 0x3fffffff))
  *(volatile byte*)(p) = 0 ;
  DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
  diva_os_wait (50) ;
 IoAdapter->InitialDspInfo |= DspCount << 16 ;
  return (ret);
}
/* -------------------------------------------------------------------------
  helper used to download dsp code toi PRI Card
  ------------------------------------------------------------------------- */
static long pri_download_buffer (OsFileHandle *fp, long length, void **addr) {
 PISDN_ADAPTER IoAdapter = (PISDN_ADAPTER)fp->sysLoadDesc ;
 dword        *sharedRam ;
 byte *p;
 *addr = (void *)IoAdapter->downloadAddr ;
 if ( ((dword) length) > IoAdapter->DspCodeBaseAddr +
                         IoAdapter->MaxDspCodeSize - IoAdapter->downloadAddr )
 {
  DBG_FTL(("%s: out of card memory during DSP download (0x%X)",
           IoAdapter->Properties.Name,
           IoAdapter->downloadAddr + length))
  return (-1) ;
 }
 p = DIVA_OS_MEM_ATTACH_RAM(IoAdapter);
 sharedRam = (dword *)(&p[DOWNLOAD_ADDR(IoAdapter)]);
 if ( fp->sysFileRead (fp, sharedRam, length) != length ) {
  DIVA_OS_MEM_DETACH_RAM(IoAdapter, p);
  return (-1) ;
 }
 IoAdapter->downloadAddr += length ;
 IoAdapter->downloadAddr  = (IoAdapter->downloadAddr + 3) & (~3) ;
 DIVA_OS_MEM_DETACH_RAM(IoAdapter, p);
 return (0) ;
}
/* -------------------------------------------------------------------------
  Download DSP code to PRI Card
  ------------------------------------------------------------------------- */
static dword pri_telindus_load (PISDN_ADAPTER IoAdapter) {
 char                *error ;
 OsFileHandle        *fp ;
 t_dsp_portable_desc  download_table[DSP_MAX_DOWNLOAD_COUNT] ;
 word                 download_count ;
 dword               *sharedRam ;
 dword                FileLength ;
 byte *p;
 if ( !(fp = OsOpenFile (DSP_TELINDUS_FILE)) )
  return (0) ;
 IoAdapter->downloadAddr = (IoAdapter->DspCodeBaseAddr
                         + sizeof(dword) + sizeof(download_table) + 3) & (~3) ;
 FileLength      = fp->sysFileSize ;
 fp->sysLoadDesc = (void *)IoAdapter ;
 fp->sysCardLoad = pri_download_buffer ;
 download_count = DSP_MAX_DOWNLOAD_COUNT ;
 memset (&download_table[0], '\0', sizeof(download_table)) ;
/*
 * set start address for download (use autoincrement mode !)
 */
 error = dsp_read_file (fp, (word)(IoAdapter->cardType),
                        &download_count, NULL, &download_table[0]) ;
 if ( error )
 {
  DBG_FTL(("download file error: %s", error))
  OsCloseFile (fp) ;
  return (0) ;
 }
 OsCloseFile (fp) ;
/*
 * store # of separate download files extracted from archive
 */
 IoAdapter->downloadAddr = IoAdapter->DspCodeBaseAddr ;
 p = DIVA_OS_MEM_ATTACH_RAM(IoAdapter);
 sharedRam = (dword *)(&p[DOWNLOAD_ADDR(IoAdapter)]);
 WRITE_DWORD(&(sharedRam[0]), (dword)download_count);
 memcpy (&sharedRam[1], &download_table[0], sizeof(download_table)) ;
 DIVA_OS_MEM_DETACH_RAM(IoAdapter, p);
 return (FileLength) ;
}
/* -------------------------------------------------------------------------
  Download PRI Card
  ------------------------------------------------------------------------- */
#define MIN_DSPS 0x30000000
static int load_pri_hardware (PISDN_ADAPTER IoAdapter) {
 dword           i ;
 struct mp_load *boot = (struct mp_load *)DIVA_OS_MEM_ATTACH_RAM(IoAdapter);
 if ( IoAdapter->Properties.Card != CARD_MAEP ) {
  DIVA_OS_MEM_DETACH_RAM(IoAdapter, boot);
  return (0) ;
 }
 boot->err = 0 ;
#if 0
 IoAdapter->rstFnc (IoAdapter) ;
#else
 if ( MIN_DSPS != (MIN_DSPS & diva_pri_detect_dsps(IoAdapter)) ) { /* makes reset */
  DIVA_OS_MEM_DETACH_RAM(IoAdapter, boot);
  DBG_FTL(("%s: DSP error!", IoAdapter->Properties.Name))
  return (0) ;
 }
#endif
/*
 * check if CPU is alive
 */
 diva_os_wait (10) ;
 i = boot->live ;
 diva_os_wait (10) ;
 if ( i == boot->live )
 {
  DIVA_OS_MEM_DETACH_RAM(IoAdapter, boot);
  DBG_FTL(("%s: CPU is not alive!", IoAdapter->Properties.Name))
  return (0) ;
 }
 if ( boot->err )
 {
  DIVA_OS_MEM_DETACH_RAM(IoAdapter, boot);
  DBG_FTL(("%s: Board Selftest failed!", IoAdapter->Properties.Name))
  return (0) ;
 }
/*
 * download protocol and dsp files
 */
 if ( !xdiSetProtocol (IoAdapter, IoAdapter->ProtocolSuffix) ) {
  DIVA_OS_MEM_DETACH_RAM(IoAdapter, boot);
  return (0) ;
 }
 if ( !pri_protocol_load (IoAdapter) ) {
  DIVA_OS_MEM_DETACH_RAM(IoAdapter, boot);
  return (0) ;
 }
 if ( !pri_telindus_load (IoAdapter) ) {
  DIVA_OS_MEM_DETACH_RAM(IoAdapter, boot);
  return (0) ;
 }
/*
 * copy configuration parameters
 */
 IoAdapter->ram += MP_SHARED_RAM_OFFSET ;
 memset (boot + MP_SHARED_RAM_OFFSET, '\0', 256) ;
 diva_configure_protocol (IoAdapter);
/*
 * start adapter
 */
 boot->addr = MP_UNCACHED_ADDR (MP_PROTOCOL_OFFSET) ;
 boot->cmd  = 3 ;
/*
 * wait for signature in shared memory (max. 3 seconds)
 */
 for ( i = 0 ; i < 300 ; ++i )
 {
  diva_os_wait (10) ;
  if ( (boot->signature >> 16) == 0x4447 )
  {
   DIVA_OS_MEM_DETACH_RAM(IoAdapter, boot);
   DBG_TRC(("Protocol startup time %d.%02d seconds",
            (i / 100), (i % 100) ))
   return (1) ;
  }
 }
 DIVA_OS_MEM_DETACH_RAM(IoAdapter, boot);
 DBG_FTL(("%s: Adapter selftest failed (0x%04X)!",
          IoAdapter->Properties.Name, boot->signature >> 16))
 pri_cpu_trapped (IoAdapter) ;
 return (0) ;
}
#else /* } { */
static int load_pri_hardware (PISDN_ADAPTER IoAdapter) {
 return (0);
}
#endif /* } */
/* --------------------------------------------------------------------------
  PRI Adapter interrupt Service Routine
   -------------------------------------------------------------------------- */
static int pri_ISR (struct _ISDN_ADAPTER* IoAdapter) {
 byte __iomem *cfg = DIVA_OS_MEM_ATTACH_CFG(IoAdapter);
 if ( !(READ_DWORD(cfg) & 0x80000000) ) {
  DIVA_OS_MEM_DETACH_CFG(IoAdapter, cfg);
  return (0) ;
 }
 /*
  clear interrupt line
  */
 WRITE_DWORD(cfg, (dword)~0x03E00000) ;
 DIVA_OS_MEM_DETACH_CFG(IoAdapter, cfg);
 IoAdapter->IrqCount++ ;
 if ( IoAdapter->Initialized )
 {
  diva_os_schedule_soft_isr (&IoAdapter->isr_soft_isr);
 }
 return (1) ;
}
/* -------------------------------------------------------------------------
  Disable interrupt in the card hardware
  ------------------------------------------------------------------------- */
static void disable_pri_interrupt (PISDN_ADAPTER IoAdapter) {
 dword volatile __iomem *cfgReg = (dword volatile __iomem *)DIVA_OS_MEM_ATTACH_CFG(IoAdapter) ;
 WRITE_DWORD(&cfgReg[3], 0);
 WRITE_DWORD(&cfgReg[1], 0);
 WRITE_DWORD(&cfgReg[0], (dword)(~0x03E00000)) ;
 DIVA_OS_MEM_DETACH_CFG(IoAdapter, cfgReg);
}
/* -------------------------------------------------------------------------
  Install entry points for PRI Adapter
  ------------------------------------------------------------------------- */
static void prepare_common_pri_functions (PISDN_ADAPTER IoAdapter) {
 ADAPTER *a = &IoAdapter->a ;
 a->ram_in           = mem_in ;
 a->ram_inw          = mem_inw ;
 a->ram_in_buffer    = mem_in_buffer ;
 a->ram_look_ahead   = mem_look_ahead ;
 a->ram_out          = mem_out ;
 a->ram_outw         = mem_outw ;
 a->ram_out_buffer   = mem_out_buffer ;
 a->ram_inc          = mem_inc ;
 a->ram_offset       = pri_ram_offset ;
 a->ram_out_dw    = mem_out_dw;
 a->ram_in_dw    = mem_in_dw;
  a->istream_wakeup   = pr_stream;
 IoAdapter->out      = pr_out ;
 IoAdapter->dpc      = pr_dpc ;
 IoAdapter->tst_irq  = scom_test_int ;
 IoAdapter->clr_irq  = scom_clear_int ;
 IoAdapter->pcm      = (struct pc_maint *)(MIPS_MAINT_OFFS
                                        - MP_SHARED_RAM_OFFSET) ;
 IoAdapter->load     = load_pri_hardware ;
 IoAdapter->disIrq   = disable_pri_interrupt ;
 IoAdapter->rstFnc   = reset_pri_hardware ;
 IoAdapter->stop     = stop_pri_hardware ;
 IoAdapter->trapFnc  = pri_cpu_trapped ;
 IoAdapter->diva_isr_handler = pri_ISR;
}
/* -------------------------------------------------------------------------
  Install entry points for PRI Adapter
  ------------------------------------------------------------------------- */
void prepare_pri_functions (PISDN_ADAPTER IoAdapter) {
 IoAdapter->MemorySize = MP_MEMORY_SIZE ;
 prepare_common_pri_functions (IoAdapter) ;
 diva_os_prepare_pri_functions (IoAdapter);
}
/* -------------------------------------------------------------------------
  Install entry points for PRI Rev.2 Adapter
  ------------------------------------------------------------------------- */
void prepare_pri2_functions (PISDN_ADAPTER IoAdapter) {
 IoAdapter->MemorySize = MP2_MEMORY_SIZE ;
 prepare_common_pri_functions (IoAdapter) ;
 diva_os_prepare_pri2_functions (IoAdapter);
}
/* ------------------------------------------------------------------------- */

/*
 *  include/asm-s390/irqextras390.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 */

#ifndef __irqextras390_h
#define __irqextras390_h

/*
  irqextras390.h by D.J. Barrow
  if you are a bitfield fan &  are paranoid that ansi dosen't
  give hard definitions about the size of an int or long you might
  prefer these definitions as an alternative.

*/

#include <linux/types.h>

typedef struct 
{
      unsigned key:4;
      unsigned s:1;
      unsigned l:1;
      unsigned cc:2;
      unsigned f:1;
      unsigned p:1;
      unsigned i:1;
      unsigned a:1;
      unsigned u:1;
      unsigned z:1;
      unsigned e:1;
      unsigned n:1;
      unsigned zero:1;
      
      unsigned fc_start:1;
      unsigned fc_halt:1;
      unsigned fc_clear:1;
      
      unsigned ac_resume_pending:1;
      unsigned ac_start_pending:1;
      unsigned ac_halt_pending:1;
      unsigned ac_clear_pending:1;
      unsigned ac_subchannel_active:1;
      unsigned ac_device_active:1;
      unsigned ac_suspended:1;
      
      unsigned sc_alert:1;
      unsigned sc_intermediate:1;
      unsigned sc_primary:1;
      unsigned sc_seconary:1;
      unsigned sc_status_pending:1;
      
      __u32	   ccw_address;
      
      unsigned dev_status_attention:1;
      unsigned dev_status_modifier:1;
      unsigned dev_status_control_unit_end:1;
      unsigned dev_status_busy:1;
      unsigned dev_status_channel_end:1;
      unsigned dev_status_device_end:1;
      unsigned dev_status_unit_check:1;
      unsigned dev_status_unit_exception:1;
      
      unsigned sch_status_program_cont_int:1;
      unsigned sch_status_incorrect_length:1;
      unsigned sch_status_program_check:1;
      unsigned sch_status_protection_check:1;
      unsigned sch_status_channel_data_check:1;
      unsigned sch_status_channel_control_check:1;
      unsigned sch_status_interface_control_check:1;
      unsigned sch_status_chaining_check:1;
     
      __u16	   byte_count;
} scsw_bits_t __attribute__((packed));

typedef struct
{
  __u32      flags;
  __u32      ccw_address;
  __u8       dev_status;
  __u8       sch_status;
  __u16      byte_count;
} scsw_words_t __attribute__((packed));

typedef struct
{
      __u8     cmd_code;
      
      unsigned cd:1;
      unsigned cc:1;
      unsigned sli:1;
      unsigned skip:1;
      unsigned pci:1;
      unsigned ida:1;
      unsigned s:1;
      unsigned res1:1;

      __u16	count;

      void	*ccw_data_address;
} ccw1_bits_t __attribute__((packed,aligned(8)));

typedef struct
{
      __u32		interruption_parm;
      unsigned key:4;
      unsigned s:1;
      unsigned res1:3;
      unsigned f:1;
      unsigned p:1;
      unsigned i:1;
      unsigned a:1;
      unsigned u:1;
      __u8	   lpm;
      unsigned l:1;
      unsigned res2:7;
      ccw1_bits_t	*ccw_program_address;
} orb_bits_t __attribute__((packed));

void fixchannelprogram(orb_bits_t *orbptr);
void fixccws(ccw1_bits_t *ccwptr);
enum
{
   ccw_write=0x1,
   ccw_read=0x2,
   ccw_read_backward=0xc,
   ccw_control=0x3,
   ccw_sense=0x4,
   ccw_sense_id=0xe4,
   ccw_transfer_in_channel0=0x8,
   ccw_transfer_in_channel1=0x8,
   ccw_set_x_mode=0xc3,		// according to uli's lan notes
   ccw_nop=0x3			// according to uli's notes again
                                        // n.b. ccw_control clashes with this
                                        // so I presume its a special case of
					// control
};



#endif








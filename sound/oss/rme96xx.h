/* (C) 2000 Guenter Geiger <geiger@debian.org>
   with copy/pastes from the driver of Winfried Ritsch <ritsch@iem.kug.ac.at>
*/


#ifndef AFMT_S32_BLOCKED
#define AFMT_S32_BLOCKED 0x0000400
#endif

#ifndef AFMT_S16_BLOCKED 
#define AFMT_S16_BLOCKED 0x0000800
#endif


typedef struct rme_status {
     unsigned int irq:1;    /* high or low */
     unsigned int lockmask:3;  /* ADAT1, ADAT2, ADAT3 */
     unsigned int sr48:1;   /* current sample rate */
     unsigned int wclock:1; /* wordclock used ? */
     unsigned int bufpoint:10;

     unsigned int syncmask:3;  /* ADAT1, ADAT2, ADAT3 */
     unsigned int doublespeed:1;
     unsigned int tc_busy:1;
     unsigned int tc_out:1;
     unsigned int crystalrate:3;
     unsigned int spdif_error:1;
     unsigned int bufid:1;
     unsigned int tc_valid:1;     
} rme_status_t;


typedef struct rme_control {
     unsigned int start:1;
     unsigned int latency:3;

     unsigned int master:1;
     unsigned int ie:1;
     unsigned int sr48:1;
     unsigned int spare:1;

     unsigned int doublespeed:1;
     unsigned int pro:1;
     unsigned int emphasis:1;
     unsigned int dolby:1;

     unsigned int opt_out:1;
     unsigned int wordclock:1;
     unsigned int spdif_in:2;

     unsigned int sync_ref:2;
} rme_ctrl_t;


typedef struct _rme_mixer {
     int i_offset;
     int o_offset;
     int devnr;
     int spare[8];
} rme_mixer;


#ifndef AUDIOCHIP_H
#define AUDIOCHIP_H

/* ---------------------------------------------------------------------- */

#define MIN(a,b) (((a)>(b))?(b):(a))
#define MAX(a,b) (((a)>(b))?(a):(b))

/* v4l device was opened in Radio mode */
#define AUDC_SET_RADIO        _IO('m',2)
/* select from TV,radio,extern,MUTE */
#define AUDC_SET_INPUT        _IOW('m',17,int)

/* audio inputs */
#define AUDIO_TUNER        0x00
#define AUDIO_RADIO        0x01
#define AUDIO_EXTERN       0x02
#define AUDIO_INTERN       0x03
#define AUDIO_OFF          0x04 
#define AUDIO_ON           0x05
#define AUDIO_MUTE         0x80
#define AUDIO_UNMUTE       0x81

/* all the stuff below is obsolete and just here for reference.  I'll
 * remove it once the driver is tested and works fine.
 *
 * Instead creating alot of tiny API's for all kinds of different
 * chips, we'll just pass throuth the v4l ioctl structs (v4l2 not
 * yet...).  It is a bit less flexible, but most/all used i2c chips
 * make sense in v4l context only.  So I think that's acceptable...
 */

#if 0

/* TODO (if it is ever [to be] accessible in the V4L[2] spec):
 *   maybe fade? (back/front)
 * notes:
 * NEWCHANNEL and SWITCH_MUTE are here because the MSP3400 has a special
 * routine to go through when it tunes in to a new channel before turning
 * back on the sound.
 * Either SET_RADIO, NEWCHANNEL, and SWITCH_MUTE or SET_INPUT need to be
 * implemented (MSP3400 uses SET_RADIO to select inputs, and SWITCH_MUTE for
 * channel-change mute -- TEA6300 et al use SET_AUDIO to select input [TV, 
 * radio, external, or MUTE]).  If both methods are implemented, you get a
 * cookie for doing such a good job! :)
 */

#define AUDC_SET_TVNORM       _IOW('m',1,int)  /* TV mode + PAL/SECAM/NTSC  */
#define AUDC_NEWCHANNEL       _IO('m',3)       /* indicate new chan - off mute */

#define AUDC_GET_VOLUME_LEFT  _IOR('m',4,__u16)
#define AUDC_GET_VOLUME_RIGHT _IOR('m',5,__u16)
#define AUDC_SET_VOLUME_LEFT  _IOW('m',6,__u16)
#define AUDC_SET_VOLUME_RIGHT _IOW('m',7,__u16)

#define AUDC_GET_STEREO       _IOR('m',8,__u16)
#define AUDC_SET_STEREO       _IOW('m',9,__u16)

#define AUDC_GET_DC           _IOR('m',10,__u16)/* ??? */

#define AUDC_GET_BASS         _IOR('m',11,__u16)
#define AUDC_SET_BASS         _IOW('m',12,__u16)
#define AUDC_GET_TREBLE       _IOR('m',13,__u16)
#define AUDC_SET_TREBLE       _IOW('m',14,__u16)

#define AUDC_GET_UNIT         _IOR('m',15,int) /* ??? - unimplemented in MSP3400 */
#define AUDC_SWITCH_MUTE      _IO('m',16)      /* turn on mute */
#endif

#endif /* AUDIOCHIP_H */

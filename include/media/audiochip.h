#ifndef AUDIOCHIP_H
#define AUDIOCHIP_H

/* ---------------------------------------------------------------------- */

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

/* misc stuff to pass around config info to i2c chips */
#define AUDC_CONFIG_PINNACLE  _IOW('m',32,int)

#endif /* AUDIOCHIP_H */

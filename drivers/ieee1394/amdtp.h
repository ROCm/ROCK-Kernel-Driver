/* -*- c-basic-offset: 8 -*- */

#ifndef __AMDTP_H
#define __AMDTP_H

#include <asm/ioctl.h>
#include <asm/types.h>

/* The userspace interface for the Audio & Music Data Transmission
 * Protocol driver is really simple. First, open /dev/amdtp, use the
 * ioctl to configure format, rate, dimension and either plug or
 * channel, then start writing samples.
 *
 * The formats supported by the driver are listed below.
 * AMDTP_FORMAT_RAW corresponds to the AM824 raw format, which can
 * carry any number of channels, so use this if you're streaming
 * multichannel audio.  The AMDTP_FORMAT_IEC958_PCM corresponds to the
 * AM824 IEC958 encapsulation without the IEC958 data bit set, using
 * AMDTP_FORMAT_IEC958_AC3 will transmit the samples with the data bit
 * set, suitable for transmitting compressed AC-3 audio.
 *
 * The rate field specifies the transmission rate; supported values are
 * AMDTP_RATE_32KHZ, AMDTP_RATE_44K1HZ and AMDTP_RATE_48KHZ.
 *
 * The dimension field specifies the dimension of the signal, that is,
 * the number of audio channels.  Only AMDTP_FORMAT_RAW supports
 * settings greater than 2.  
 *
 * The last thing to specify is either the isochronous channel to use
 * or the output plug to connect to.  If you know what channel the
 * destination device will listen on, you can specify the channel
 * directly and use the AMDTP_IOC_CHANNEL ioctl.  However, if the
 * destination device chooses the channel and uses the IEC61883-1 plug
 * mechanism, you can specify an output plug to connect to.  The
 * driver will pick up the channel number from the plug once the
 * destination device locks the output plug control register.  In this
 * case set the plug field and use the AMDTP_IOC_PLUG ioctl.
 *
 * Having configured the interface, the driver now accepts writes of
 * regular 16 bit signed little endian samples, with the channels
 * interleaved.  For example, 4 channels would look like:
 *
 *   | sample 0                                      | sample 1    ...
 *   | ch. 0     | ch. 1     | ch. 2     | ch. 3     | ch. 0     | ...
 *   | lsb | msb | lsb | msb | lsb | msb | lsb | msb | lsb | msb | ...
 *
 */

/* We use '#' for our ioctl magic number because it's cool. */

#define AMDTP_IOC_CHANNEL _IOW('#', 0, sizeof (struct amdtp_ioctl))
#define AMDTP_IOC_PLUG    _IOW('#', 1, sizeof (struct amdtp_ioctl))
#define AMDTP_IOC_PING    _IOW('#', 2, sizeof (struct amdtp_ioctl))
#define AMDTP_IOC_ZAP     _IO('#', 3)

enum {
	AMDTP_FORMAT_RAW,
	AMDTP_FORMAT_IEC958_PCM,
	AMDTP_FORMAT_IEC958_AC3
};

enum {
	AMDTP_RATE_32KHZ,
	AMDTP_RATE_44K1HZ,
	AMDTP_RATE_48KHZ,
};

struct amdtp_ioctl {
	__u32 format;
	__u32 rate;
	__u32 dimension;
	union { __u32 channel; __u32 plug; } u;
};

#endif /* __AMDTP_H */

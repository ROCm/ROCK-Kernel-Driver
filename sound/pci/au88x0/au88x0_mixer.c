/*
 * Vortex Mixer support.
 *
 * There is much more than just the AC97 mixer...
 *
 */

#include <sound/driver.h>
#include <linux/time.h>
#include <linux/init.h>
#include <sound/core.h>
#include "au88x0.h"

static int __devinit snd_vortex_mixer(vortex_t * vortex)
{
	ac97_bus_t bus, *pbus;
	ac97_t ac97;
	int err;

	memset(&bus, 0, sizeof(bus));
	bus.write = vortex_codec_write;
	bus.read = vortex_codec_read;
	if ((err = snd_ac97_bus(vortex->card, &bus, &pbus)) < 0)
		return err;
	memset(&ac97, 0, sizeof(ac97));
	// Intialize AC97 codec stuff.
	ac97.private_data = vortex;
	return snd_ac97_mixer(pbus, &ac97, &vortex->codec);
}

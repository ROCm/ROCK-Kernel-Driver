/* 
 * Copyright (C) 2002 Steve Schmidtke 
 * Licensed under the GPL
 */

#ifndef HOSTAUDIO_H
#define HOSTAUDIO_H

#define HOSTAUDIO_DEV_DSP "/dev/sound/dsp"
#define HOSTAUDIO_DEV_MIXER "/dev/sound/mixer"

struct hostaudio_state {
  int fd;
};

struct hostmixer_state {
  int fd;
};

/* UML user-side protoypes */
extern ssize_t hostaudio_read_user(struct hostaudio_state *state, char *buffer,
				   size_t count, loff_t *ppos);
extern ssize_t hostaudio_write_user(struct hostaudio_state *state, 
				    const char *buffer, size_t count, 
				    loff_t *ppos);
extern int hostaudio_ioctl_user(struct hostaudio_state *state, 
				unsigned int cmd, unsigned long arg);
extern int hostaudio_open_user(struct hostaudio_state *state, int r, int w, 
			       char *dsp);
extern int hostaudio_release_user(struct hostaudio_state *state);
extern int hostmixer_ioctl_mixdev_user(struct hostmixer_state *state, 
				unsigned int cmd, unsigned long arg);
extern int hostmixer_open_mixdev_user(struct hostmixer_state *state, int r, 
				      int w, char *mixer);
extern int hostmixer_release_mixdev_user(struct hostmixer_state *state);

#endif /* HOSTAUDIO_H */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

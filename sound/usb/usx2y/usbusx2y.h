#ifndef USBUSX2Y_H
#define USBUSX2Y_H
#include "../usbaudio.h"
#include "usbus428ctldefs.h" 

#define NRURBS	        2	/* */
#define NRPACKS		1	/* FIXME: Currently only 1 works.
				   usb-frames/ms per urb: 1 and 2 are supported.
				   setting to 2 will PERHAPS make it easier for slow machines.
				   Jitter will be higher though.
				   On my PIII 500Mhz Laptop setting to 1 is the only way to go 
				   for PLAYING synths. i.e. Jack & Aeolus sound quit nicely 
				   at 4 periods 64 frames. 
				*/

#define URBS_AsyncSeq 10
#define URB_DataLen_AsyncSeq 32
typedef struct {
	struct urb*	urb[URBS_AsyncSeq];
	char*   buffer;
} snd_usX2Y_AsyncSeq_t;

typedef struct {
	int	submitted;
	int	len;
	struct urb*	urb[0];
} snd_usX2Y_urbSeq_t;

typedef struct snd_usX2Y_substream snd_usX2Y_substream_t;

typedef struct {
	snd_usb_audio_t 	chip;
	int			stride;
	struct urb		*In04urb;
	void			*In04Buf;
	char			In04Last[24];
	unsigned		In04IntCalls;
	snd_usX2Y_urbSeq_t	*US04;
	wait_queue_head_t	In04WaitQueue;
	snd_usX2Y_AsyncSeq_t	AS04;
	unsigned int		rate,
				format;
	int			refframes;
	int			chip_status;
	struct semaphore	open_mutex;
	us428ctls_sharedmem_t	*us428ctls_sharedmem;
	wait_queue_head_t	us428ctls_wait_queue_head;
	snd_usX2Y_substream_t	*substream[4];
} usX2Ydev_t;


#define usX2Y(c) ((usX2Ydev_t*)(c)->private_data)

int usX2Y_audio_create(snd_card_t* card);

int usX2Y_AsyncSeq04_init(usX2Ydev_t* usX2Y);
int usX2Y_In04_init(usX2Ydev_t* usX2Y);

#define NAME_ALLCAPS "US-X2Y"

#endif

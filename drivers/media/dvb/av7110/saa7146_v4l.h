#ifndef __INCLUDED_SAA7146_V4L_V4L__
#define __INCLUDED_SAA7146_V4L_V4L__

/************************************************************************/
/* ADDRESSING 								*/
/************************************************************************/

#define SAA7146_V4L_BASE	100

#define	SAA7146_V4L_GPICT	    	_IOW('d', (SAA7146_V4L_BASE+ 1), struct video_picture)
#define	SAA7146_V4L_SPICT	    	_IOW('d', (SAA7146_V4L_BASE+ 2), struct video_picture)

#define	SAA7146_V4L_GFBUF	    	_IOW('d', (SAA7146_V4L_BASE+ 3), struct video_buffer)
#define	SAA7146_V4L_SFBUF	    	_IOW('d', (SAA7146_V4L_BASE+ 4), struct video_buffer)

#define	SAA7146_V4L_GMBUF	    	_IOW('d', (SAA7146_V4L_BASE+ 5), struct video_mbuf)

#define	SAA7146_V4L_SWIN	    	_IOW('d', (SAA7146_V4L_BASE+ 6), struct video_window)

#define	SAA7146_V4L_CCAPTURE    	_IOW('d', (SAA7146_V4L_BASE+ 7), int)

#define	SAA7146_V4L_CMCAPTURE		_IOW('d', (SAA7146_V4L_BASE+ 8), struct video_mmap)
#define	SAA7146_V4L_CSYNC    		_IOW('d', (SAA7146_V4L_BASE+ 9), int)
#define	SAA7146_V4L_CGSTATUS   		_IOW('d', (SAA7146_V4L_BASE+10), int)

#define	SAA7146_V4L_TSCAPTURE  		_IOW('d', (SAA7146_V4L_BASE+11), int)

extern int saa7146_v4l_init (void);
extern void saa7146_v4l_exit (void);

#endif


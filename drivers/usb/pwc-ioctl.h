/* (C) 2001 Nemosoft Unv.    webcam@smcc.demon.nl
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef PWC_IOCTL_H
#define PWC_IOCTL_H

/* These are private ioctl() commands, specific for the Philips webcams.
   They contain functions not found in other webcams, and settings not
   specified in the Video4Linux API. 
   
   The #define names are built up like follows:
   VIDIOC		VIDeo IOCtl prefix
         PWC		Philps WebCam
            G           optional: Get
            S           optional: Set
             ... 	the function
 */




/* The frame rate is encoded in the video_window.flags parameter using
   the upper 16 bits, since some flags are defined nowadays. The following
   defines provide a mask and shift to filter out this value.
   
   In 'Snapshot' mode the camera freezes its automatic exposure and colour 
   balance controls.
 */
#define PWC_FPS_SHIFT		16
#define PWC_FPS_MASK		0x00FF0000
#define PWC_FPS_FRMASK		0x003F0000
#define PWC_FPS_SNAPSHOT	0x00400000


 /* Restore user settings */
#define VIDIOCPWCRUSER		_IO('v', 192)
 /* Save user settings */
#define VIDIOCPWCSUSER		_IO('v', 193)
 /* Restore factory settings */
#define VIDIOCPWCFACTORY	_IO('v', 194)

 /* You can manipulate the compression factor. A compression preference of 0
    means use uncompressed modes when available; 1 is low compression, 2 is
    medium and 3 is high compression preferred. Of course, the higher the
    compression, the lower the bandwidth used but more chance of artefacts
    in the image. The driver automatically chooses a higher compression when
    the preferred mode is not available.
  */
 /* Set preferred compression quality (0 = uncompressed, 3 = highest compression) */
#define VIDIOCPWCSCQUAL		_IOW('v', 195, int)
 /* Get preferred compression quality */
#define VIDIOCPWCGCQUAL		_IOR('v', 195, int)

 /* Set AGC (Automatic Gain Control); int < 0 = auto, 0..65535 = fixed */
#define VIDIOCPWCSAGC		_IOW('v', 200, int)
 /* Get AGC; int < 0 = auto; >= 0 = fixed, range 0..65535 */
#define VIDIOCPWCGAGC		_IOR('v', 200, int)
 /* Set shutter speed; int < 0 = auto; >= 0 = fixed, range 0..65535 */
#define VIDIOCPWCSSHUTTER	_IOW('v', 201, int)

#endif

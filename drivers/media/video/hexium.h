#ifndef __HEXIUM__
#define __HEXIUM__

#define HEXIUM_HV_PCI6_ORION		1
#define HEXIUM_ORION_1SVHS_3BNC		2
#define HEXIUM_ORION_4BNC		3
#define HEXIUM_GEMUINI			4
#define HEXIUM_GEMUINI_DUAL		5

static struct saa7146_standard hexium_standards[] = {
	{
		.name	= "PAL", 	.id	= V4L2_STD_PAL,
		.v_offset	= 0x17,	.v_field 	= 288,	.v_calc		= 576,
		.h_offset	= 0x14,	.h_pixels 	= 680,	.h_calc		= 680+1,
		.v_max_out	= 576,	.h_max_out	= 768,
	}, {
		.name	= "NTSC", 	.id	= V4L2_STD_NTSC,
		.v_offset	= 0x17,	.v_field 	= 240,	.v_calc		= 480,
		.h_offset	= 0x06,	.h_pixels 	= 640,	.h_calc		= 641+1,
		.v_max_out	= 480,	.h_max_out	= 640,
	}, {
		.name	= "SECAM", 	.id	= V4L2_STD_SECAM,
		.v_offset	= 0x14,	.v_field 	= 288,	.v_calc		= 576,
		.h_offset	= 0x14,	.h_pixels 	= 720,	.h_calc		= 720+1,
		.v_max_out	= 576,	.h_max_out	= 768,
	}
};		


#define HEXIUM_INPUTS	9
static struct v4l2_input hexium_inputs[HEXIUM_INPUTS] = {
	{ 0, "CVBS 1",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ 1, "CVBS 2",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ 2, "CVBS 3",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ 3, "CVBS 4",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ 4, "CVBS 5",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ 5, "CVBS 6",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ 6, "Y/C 1",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ 7, "Y/C 2",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
	{ 8, "Y/C 3",	V4L2_INPUT_TYPE_CAMERA,	2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
};

#define HEXIUM_AUDIOS	0

struct hexium_data
{
	s8 adr;
	u8 byte;
};

#endif

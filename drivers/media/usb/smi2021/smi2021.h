/************************************************************************
 * smi2021.h								*
 *									*
 * USB Driver for SMI2021 - EasyCap					*
 * **********************************************************************
 *
 * Copyright 2011-2013 Jon Arne Jørgensen
 * <jonjon.arnearne--a.t--gmail.com>
 *
 * Copyright 2011, 2012 Tony Brown, Michal Demin, Jeffry Johnston
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This driver is heavily influensed by the STK1160 driver.
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 */

#ifndef SMI2021_H
#define SMI2021_H

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/saa7115.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>

#ifndef GITVERSION
#define GITVERSION ""
#endif

#define SMI2021_DRIVER_VERSION "0.1"GITVERSION

#define SMI2021_ISOC_TRANSFERS	16
#define SMI2021_ISOC_PACKETS	10
#define SMI2021_ISOC_EP		0x82

/* General USB control setup */
#define SMI2021_USB_REQUEST	0x01
#define SMI2021_USB_INDEX	0x00
#define SMI2021_USB_SNDPIPE	0x00
#define SMI2021_USB_RCVPIPE	0x80

/* General video constants */
#define SMI2021_BYTES_PER_LINE	1440
#define SMI2021_PAL_LINES	576
#define SMI2021_NTSC_LINES	484

/* Timing Referance Codes, see saa7113 datasheet */
#define SMI2021_TRC_EAV		0x10
#define SMI2021_TRC_VBI		0x20
#define SMI2021_TRC_FIELD_2	0x40
#define SMI2021_TRC		0x80

#ifdef DEBUG
#define smi2021_dbg(fmt, args...)		\
	pr_debug("smi2021::%s: " fmt, __func__, \
			##args)
#else
#define smi2021_dbg(fmt, args...)
#endif

#define smi2021_info(fmt, args...)		\
	pr_info("smi2021::%s: " fmt,		\
		__func__, ##args)

#define smi2021_warn(fmt, args...)		\
	pr_warn("smi2021::%s: " fmt,		\
		__func__, ##args)

#define smi2021_err(fmt, args...)		\
	pr_err("smi2021::%s: " fmt,		\
		__func__, ##args)

/* Structs passed on USB for device setup */
struct smi2021_set_hw_state {
	u8 head;
	u8 state;
} __packed;

/* A single videobuf2 frame buffer */
struct smi2021_buf {
	/* Common vb2 stuff, must be first */
	struct vb2_buffer		vb;
	struct list_head		list;

	void				*mem;
	unsigned int			length;

	bool				active;
	bool				second_field;
	bool				in_blank;
	unsigned int			pos;

	/* ActiveVideo - Line counter */
	u16				trc_av;
};

struct smi2021_vid_input {
	char				*name;
	int				type;
};

enum smi2021_sync {
	HSYNC,
	SYNCZ1,
	SYNCZ2,
	TRC
};

struct gm7113c_init_overrides {
	enum saa7113_r10_ofts		r10_ofts;
	bool 				r10_vrln;
	bool				r13_adlsb;
};

struct smi2021_isoc_ctl {
	/* max packet size of isoc transaction */
	int max_pkt_size;

	/* number of allocated urbs */
	int num_bufs;

	/* urb for isoc transfers */
	struct urb **urb;

	/* transfer buffers for isoc transfer */
	char **transfer_buffer;

};

struct smi2021 {
	struct device			*dev;
	struct usb_device		*udev;
	struct i2c_adapter		i2c_adap;
	struct i2c_client		i2c_client;
	struct v4l2_ctrl_handler	ctrl_handler;

	/* i2c subdevice setup */
	struct i2c_board_info		gm7113c_info;
	struct gm7113c_init_overrides	gm7113c_overrides;
	struct saa7115_platform_data	gm7113c_platform_data;
	struct v4l2_subdev		*gm7113c_subdev;

	struct v4l2_device		v4l2_dev;
	struct video_device		vdev;
	struct vb2_queue		vb_vidq;
	struct mutex			v4l2_lock;
	struct mutex			vb_queue_lock;

	struct smi2021_isoc_ctl		isoc_ctl;

	/* List of videobuf2 buffers protected by a lock. */
	spinlock_t			buf_lock;
	struct list_head		avail_bufs;
	struct smi2021_buf		*cur_buf;

	int				sequence;

	/* Frame settings */
	int				cur_height;
	v4l2_std_id			cur_norm;
	enum smi2021_sync		sync_state;

	struct snd_card			*snd_card;
	struct snd_pcm_substream	*pcm_substream;

	unsigned int			pcm_write_ptr;
	unsigned int			pcm_complete_samples;

	u8				pcm_read_offset;
	struct work_struct		adev_capture_trigger;
	atomic_t			adev_capturing;

	/* Device settings */
	unsigned int			vid_input_count;
	const struct smi2021_vid_input	*vid_inputs;
	int				cur_input;

	int				iso_size;

	struct {
		unsigned int 	good_bufs;
		unsigned int	bad_bufs;
		unsigned int	ovflw_bufs;
	}				stats;
};

/* Provided by smi2021_bootloader.c */
int smi2021_bootloader_probe(struct usb_interface *intf,
					const struct usb_device_id *devid);
void smi2021_bootloader_disconnect(struct usb_interface *intf);

/* Provided by smi2021_main.c */
void smi2021_toggle_audio(struct smi2021 *smi2021, bool enable);
int smi2021_start(struct smi2021 *smi2021);
int smi2021_stop(struct smi2021 *smi2021);

/* Provided by smi2021_v4l2.c */
int smi2021_vb2_setup(struct smi2021 *smi2021);
int smi2021_video_register(struct smi2021 *smi2021);
void smi2021_clear_queue(struct smi2021 *smi2021);

/* Provided by smi2021_audio.c */
int smi2021_snd_register(struct smi2021 *smi2021);
void smi2021_snd_unregister(struct smi2021 *smi2021);
void smi2021_stop_audio(struct smi2021 *smi2021);
void smi2021_audio(struct smi2021 *smi2021, u8 *data, int len);
#endif /* SMI2021_H */

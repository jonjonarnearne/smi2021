/************************************************************************
 * smi2021_v4l2.c							*
 *									*
 * USB Driver for smi2021 - EasyCap					*
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

#include "smi2021.h"

static struct v4l2_file_operations smi2021_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

/*
 * vidioc ioctls
 */
static int vidioc_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	struct smi2021 *smi2021 = video_drvdata(file);

	strlcpy(cap->driver, "smi2021", sizeof(cap->driver));
	strlcpy(cap->card, "smi2021", sizeof(cap->card));
	usb_make_path(smi2021->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE |
			   V4L2_CAP_STREAMING |
			   V4L2_CAP_READWRITE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *i)
{
	struct smi2021 *smi2021 = video_drvdata(file);

	if (i->index >= smi2021->vid_input_count)
		return -EINVAL;

	strlcpy(i->name, smi2021->vid_inputs[i->index].name, sizeof(i->name));
	i->type = V4L2_INPUT_TYPE_CAMERA;
	i->std = smi2021->vdev.tvnorms;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	strlcpy(f->description, "16bpp YU2, 4:2:2, packed",
					sizeof(f->description));
	f->pixelformat = V4L2_PIX_FMT_UYVY;
	return 0;
}

static int vidioc_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct smi2021 *smi2021 = video_drvdata(file);

	f->fmt.pix.width = SMI2021_BYTES_PER_LINE / 2;
	f->fmt.pix.height = smi2021->cur_height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	f->fmt.pix.bytesperline = SMI2021_BYTES_PER_LINE;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.priv = 0;
	return 0;
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *norm)
{
	struct smi2021 *smi2021 = video_drvdata(file);

	*norm = smi2021->cur_norm;
	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct smi2021 *smi2021 = video_drvdata(file);

	*i = smi2021->cur_input;
	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id norm)
{
	struct smi2021 *smi2021 = video_drvdata(file);

	if (norm == smi2021->cur_norm)
		return 0;

	if (vb2_is_busy(&smi2021->vb_vidq))
		return -EBUSY;

	smi2021->cur_norm = norm;
	if (norm & V4L2_STD_525_60)
		smi2021->cur_height = SMI2021_NTSC_LINES;
	else if (norm & V4L2_STD_625_50)
		smi2021->cur_height = SMI2021_PAL_LINES;
	else
		return -EINVAL;

	v4l2_subdev_call(smi2021->gm7113c_subdev, video, s_std,
			smi2021->cur_norm);

	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct smi2021 *smi2021 = video_drvdata(file);

	if (i >= smi2021->vid_input_count)
		return -EINVAL;

	v4l2_subdev_call(smi2021->gm7113c_subdev, video, s_routing,
		smi2021->vid_inputs[i].type, 0, 0);

	smi2021->cur_input = i;

	return 0;
}

static const struct v4l2_ioctl_ops smi2021_ioctl_ops = {
	.vidioc_querycap		= vidioc_querycap,
	.vidioc_enum_input		= vidioc_enum_input,
	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= vidioc_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= vidioc_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= vidioc_fmt_vid_cap,
	.vidioc_g_std			= vidioc_g_std,
	.vidioc_s_std			= vidioc_s_std,
	.vidioc_g_input			= vidioc_g_input,
	.vidioc_s_input			= vidioc_s_input,

	/* vb2 handle these */
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
/*	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf, */
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	/* v4l2-event and v4l2-ctrl handle these */
	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};


/*
 * Videobuf2 operations
 */
static int queue_setup(struct vb2_queue *vq,
				const struct v4l2_format *v4l2_fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	struct smi2021 *smi2021 = vb2_get_drv_priv(vq);
	*nbuffers = clamp_t(unsigned int, *nbuffers, 4, 16);

	sizes[0] = SMI2021_BYTES_PER_LINE * smi2021->cur_height;

	/* This means a packed colorformat */
	*nplanes = 1;

	v4l2_info(&smi2021->v4l2_dev, "%s: buffer count %d, each %d bytes\n",
				__func__, *nbuffers, sizes[0]);

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	unsigned long flags;
	struct smi2021 *smi2021 = vb2_get_drv_priv(vb->vb2_queue);
	struct smi2021_buf *buf = container_of(vb, struct smi2021_buf, vb);

	spin_lock_irqsave(&smi2021->buf_lock, flags);
	if (!smi2021->udev) {
		/*
		 * If the device is disconnected return the buffer to userspace
		 * directly. The next QBUF call will fail with -ENODEV.
		 */
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	} else {
		buf->mem = vb2_plane_vaddr(vb, 0);
		buf->length = vb2_plane_size(vb, 0);
		buf->pos = 0;

		buf->trc_av = 0;
		buf->in_blank = true;
		buf->second_field = false;

		/*
		 * If the buffer length is less than expected,
		 * we return the buffer back to userspace
		 */
		if (buf->length < SMI2021_BYTES_PER_LINE * smi2021->cur_height)
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		else
			list_add_tail(&buf->list, &smi2021->avail_bufs);
	}
	spin_unlock_irqrestore(&smi2021->buf_lock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct smi2021 *smi2021 = vb2_get_drv_priv(vq);
	return smi2021_start(smi2021);
}

static void stop_streaming(struct vb2_queue *vq)
{
	struct smi2021 *smi2021 = vb2_get_drv_priv(vq);
	smi2021_stop(smi2021);
}

static struct vb2_ops smi2021_video_qops = {
	.queue_setup		= queue_setup,
	.buf_queue		= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* Could possibly use V4L2_STD_ALL */
static struct video_device v4l_template = {
	.name			= "smi2021",
	.tvnorms		= V4L2_STD_525_60 | V4L2_STD_625_50,
	.fops			= &smi2021_fops,
	.ioctl_ops		= &smi2021_ioctl_ops,
	.release		= video_device_release_empty,
};

/*****************************************************************************/

/* Must be called with both v4l2_lock and vb_queue_lock held */
void smi2021_clear_queue(struct smi2021 *smi2021)
{
	struct smi2021_buf *buf;
	unsigned long flags;

	dev_info(smi2021->dev, "clear_queue called\n");

	/* Release all active buffers */
	spin_lock_irqsave(&smi2021->buf_lock, flags);
	while (!list_empty(&smi2021->avail_bufs)) {
		buf = list_first_entry(&smi2021->avail_bufs,
				struct smi2021_buf, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		dev_info(smi2021->dev, "buffer [%p/%d] aborted\n",
				buf, buf->vb.v4l2_buf.index);
	}
	/* It's important to clear current buffer */
	if (smi2021->cur_buf) {
		buf = smi2021->cur_buf;
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		dev_info(smi2021->dev, "buffer [%p/%d] aborted\n",
				buf, buf->vb.v4l2_buf.index);
	}
	smi2021->cur_buf = NULL;
	spin_unlock_irqrestore(&smi2021->buf_lock, flags);
	dev_info(smi2021->dev, "returning from clear_queue\n");
}

int smi2021_vb2_setup(struct smi2021 *smi2021)
{
	int rc;
	struct vb2_queue *q;

	q = &smi2021->vb_vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_READ | VB2_MMAP | VB2_USERPTR;
	q->drv_priv = smi2021;
	q->buf_struct_size = sizeof(struct smi2021_buf);
	q->ops = &smi2021_video_qops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	rc = vb2_queue_init(q);
	if (rc < 0)
		return rc;

	INIT_LIST_HEAD(&smi2021->avail_bufs);

	return 0;
}

int smi2021_video_register(struct smi2021 *smi2021)
{
	int rc;

	/* Initalize video_device with a template structure */
	smi2021->vdev = v4l_template;
	smi2021->vdev.debug = 0;			/* enable debug */
	smi2021->vdev.queue = &smi2021->vb_vidq;

	/*
	 * Provide mutexes for v4l2 core and for videobuf2 queue.
	 * It will be used to protect *only* v4l2 ioctls.
	 */
	smi2021->vdev.lock = &smi2021->v4l2_lock;
	smi2021->vdev.queue->lock = &smi2021->vb_queue_lock;

	/* This will be used to set video_device parent */
	smi2021->vdev.v4l2_dev = &smi2021->v4l2_dev;

	video_set_drvdata(&smi2021->vdev, smi2021);
	rc = video_register_device(&smi2021->vdev, VFL_TYPE_GRABBER, -1);
	if (rc < 0) {
		dev_err(smi2021->dev, "video_register_device failed (%d)\n",
									rc);
		return rc;
	}

	v4l2_info(&smi2021->v4l2_dev, "V4L2 device registered as %s\n",
				video_device_node_name(&smi2021->vdev));

	return 0;
}

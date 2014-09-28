/************************************************************************
 * smi2021_audio.c							*
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

#include "smi2021.h"

static void pcm_buffer_free(struct snd_pcm_substream *substream)
{
	vfree(substream->runtime->dma_area);
	substream->runtime->dma_area = NULL;
	substream->runtime->dma_bytes = 0;
}

static int pcm_buffer_alloc(struct snd_pcm_substream *substream, int size)
{
	if (substream->runtime->dma_area) {
		if (substream->runtime->dma_bytes > size)
			return 0;
		pcm_buffer_free(substream);
	}

	substream->runtime->dma_area = vmalloc(size);
	if (substream->runtime->dma_area == NULL)
		return -ENOMEM;

	substream->runtime->dma_bytes = size;

	return 0;
}

static const struct snd_pcm_hardware smi2021_pcm_hw = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_INTERLEAVED    |
		SNDRV_PCM_INFO_MMAP           |
		SNDRV_PCM_INFO_MMAP_VALID     |
		SNDRV_PCM_INFO_BATCH,

	.formats = SNDRV_PCM_FMTBIT_S32_LE,

	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.period_bytes_min = 992,	/* 32640 */ /* 15296 */
	.period_bytes_max = 15872,	/* 65280 */
	.periods_min = 1,		/* 1 */
	.periods_max = 16,		/* 2 */
	.buffer_bytes_max = 65280,	/* 65280 */
};

static int smi2021_pcm_open(struct snd_pcm_substream *substream)
{
	struct smi2021 *smi2021 = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int rc;

	rc = snd_pcm_hw_constraint_pow2(runtime, 0,
					SNDRV_PCM_HW_PARAM_PERIODS);
	if (rc < 0)
		return rc;

	smi2021->pcm_substream = substream;

	runtime->hw = smi2021_pcm_hw;
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	return 0;
}

static int smi2021_pcm_close(struct snd_pcm_substream *substream)
{
	struct smi2021 *smi2021 = snd_pcm_substream_chip(substream);

	if (atomic_read(&smi2021->adev_capturing)) {
		atomic_set(&smi2021->adev_capturing, 0);
		schedule_work(&smi2021->adev_capture_trigger);
	}
	return 0;

}


static int smi2021_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params)
{
	int size, rc;
	size = params_period_bytes(hw_params) * params_periods(hw_params);

	rc = pcm_buffer_alloc(substream, size);
	if (rc < 0)
		return rc;


	return 0;
}

static int smi2021_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct smi2021 *smi2021 = snd_pcm_substream_chip(substream);

	if (atomic_read(&smi2021->adev_capturing)) {
		atomic_set(&smi2021->adev_capturing, 0);
		schedule_work(&smi2021->adev_capture_trigger);
	}

	pcm_buffer_free(substream);
	return 0;
}

static int smi2021_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct smi2021 *smi2021 = snd_pcm_substream_chip(substream);

	smi2021->pcm_complete_samples = 0;
	smi2021->pcm_read_offset = 0;
	smi2021->pcm_write_ptr = 0;

	return 0;
}

static void capture_trigger(struct work_struct *work)
{
	struct smi2021 *smi2021 = container_of(work, struct smi2021,
						adev_capture_trigger);

	if (atomic_read(&smi2021->adev_capturing))
		smi2021_toggle_audio(smi2021, true);
	else
		smi2021_toggle_audio(smi2021, false);
}

/* This callback is ATOMIC, must not sleep */
static int smi2021_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct smi2021 *smi2021 = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
		atomic_set(&smi2021->adev_capturing, 1);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		atomic_set(&smi2021->adev_capturing, 0);
		break;
	default:
		return -EINVAL;
	}

	schedule_work(&smi2021->adev_capture_trigger);

	return 0;
}

static snd_pcm_uframes_t smi2021_pcm_pointer(
				struct snd_pcm_substream *substream)
{
	struct smi2021 *smi2021 = snd_pcm_substream_chip(substream);
	return smi2021->pcm_write_ptr / 8;
}

static struct page *smi2021_pcm_get_vmalloc_page(
					struct snd_pcm_substream *subs,
					unsigned long offset)
{
	void *pageptr = subs->runtime->dma_area + offset;

	return vmalloc_to_page(pageptr);
}

static struct snd_pcm_ops smi2021_pcm_ops = {
	.open = smi2021_pcm_open,
	.close = smi2021_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = smi2021_pcm_hw_params,
	.hw_free = smi2021_pcm_hw_free,
	.prepare = smi2021_pcm_prepare,
	.trigger = smi2021_pcm_trigger,
	.pointer = smi2021_pcm_pointer,
	.page = smi2021_pcm_get_vmalloc_page,
};

int smi2021_snd_register(struct smi2021 *smi2021)
{
	struct snd_card	*card;
	struct snd_pcm *pcm;
	int rc = 0;

	rc = snd_card_new(smi2021->dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			THIS_MODULE, 0, &card);
	if (rc < 0)
		return rc;

	rc = snd_pcm_new(card, "smi2021 Audio", 0, 0, 1, &pcm);
	if (rc < 0)
		goto err_free_card;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &smi2021_pcm_ops);
	pcm->info_flags = 0;
	pcm->private_data = smi2021;
	strcpy(pcm->name, "Somagic smi2021 Capture");

	strcpy(card->driver, "smi2021-Audio");
	strcpy(card->shortname, "smi2021 Audio");
	strcpy(card->longname, "Somagic smi2021 Audio");

	INIT_WORK(&smi2021->adev_capture_trigger, capture_trigger);

	rc = snd_card_register(card);
	if (rc < 0)
		goto err_free_card;

	smi2021->snd_card = card;

	return 0;

err_free_card:
	snd_card_free(card);
	return rc;
}

void smi2021_snd_unregister(struct smi2021 *smi2021)
{
	if (smi2021 == NULL)
		return;

	if (smi2021->snd_card == NULL)
		return;

	snd_card_free(smi2021->snd_card);
	smi2021->snd_card = NULL;
}

void smi2021_stop_audio(struct smi2021 *smi2021)
{
	/*
	 * HACK: Stop the audio subsystem,
	 * without this, the pcm middle-layer will hang waiting for more data.
	 *
	 * Is there a better way to do this?
	 */
/*
	if (smi2021->pcm_substream && smi2021->pcm_substream->runtime) {
		struct snd_pcm_runtime *runtime;

		runtime = smi2021->pcm_substream->runtime;
		if (runtime->status) {
			runtime->status->state = SNDRV_PCM_STATE_DRAINING;
			wake_up(&runtime->sleep);
		}
	}
*/
}

void smi2021_audio(struct smi2021 *smi2021, u8 *data, int len)
{
	struct snd_pcm_runtime *runtime;
	u8 offset;
	int new_offset = 0;

	int skip;
	unsigned int stride, oldptr, headptr;

	int diff = 0;
	int samples = 0;
	bool period_elapsed = false;


	if (smi2021->udev == NULL)
		return;

	if (atomic_read(&smi2021->adev_capturing) == 0)
		return;

	if (smi2021->pcm_substream == NULL)
		return;

	runtime = smi2021->pcm_substream->runtime;
	if (!runtime || !runtime->dma_area)
		return;

	offset = smi2021->pcm_read_offset;
	stride = runtime->frame_bits >> 3;

	if (stride == 0)
		return;

	diff = smi2021->pcm_write_ptr;

	/*
	 * Check that the end of the last buffer was correct.
	 * If not correct, we mark any partial frames in buffer as complete
	 */
	headptr = smi2021->pcm_write_ptr - offset - 4;
	if (smi2021->pcm_write_ptr > 10
	    && runtime->dma_area[headptr] != 0x00) {
		skip = stride - (smi2021->pcm_write_ptr % stride);
		snd_pcm_stream_lock(smi2021->pcm_substream);
		smi2021->pcm_write_ptr += skip;

		if (smi2021->pcm_write_ptr >= runtime->dma_bytes)
			smi2021->pcm_write_ptr -= runtime->dma_bytes;

		snd_pcm_stream_unlock(smi2021->pcm_substream);
		offset = smi2021->pcm_read_offset = 0;
	}
	/*
	 * The device is actually sending 24Bit pcm data
	 * with 0x00 as the header byte before each sample.
	 * We look for this byte to make sure we did not
	 * loose any bytes during transfer.
	 */
	while (len > stride && (data[offset] != 0x00 ||
			data[offset + (stride / 2)] != 0x00)) {
		new_offset++;
		data++;
		len--;
	}

	if (len <= stride) {
		/* We exhausted the buffer looking for 0x00 */
		smi2021->pcm_read_offset = 0;
		return;
	}
	if (new_offset != 0) {
		/*
		 * This buffer can not be appended to the current buffer,
		 * so we mark any partial frames in the buffer as complete.
		 */
		skip = stride - (smi2021->pcm_write_ptr % stride);
		snd_pcm_stream_lock(smi2021->pcm_substream);
		smi2021->pcm_write_ptr += skip;

		if (smi2021->pcm_write_ptr >= runtime->dma_bytes)
			smi2021->pcm_write_ptr -= runtime->dma_bytes;

		snd_pcm_stream_unlock(smi2021->pcm_substream);

		offset = smi2021->pcm_read_offset = new_offset % (stride / 2);

	}

	oldptr = smi2021->pcm_write_ptr;
	if (oldptr + len >= runtime->dma_bytes) {
		unsigned int cnt = runtime->dma_bytes - oldptr;
		memcpy(runtime->dma_area + oldptr, data, cnt);
		memcpy(runtime->dma_area, data + cnt, len - cnt);
	} else {
		memcpy(runtime->dma_area + oldptr, data, len);
	}

	snd_pcm_stream_lock(smi2021->pcm_substream);
	smi2021->pcm_write_ptr += len;

	if (smi2021->pcm_write_ptr >= runtime->dma_bytes)
		smi2021->pcm_write_ptr -= runtime->dma_bytes;

	samples = smi2021->pcm_write_ptr - diff;
	if (samples < 0)
		samples += runtime->dma_bytes;

	samples /= (stride / 2);

	smi2021->pcm_complete_samples += samples;
	if (smi2021->pcm_complete_samples / 2 >= runtime->period_size) {
		smi2021->pcm_complete_samples -= runtime->period_size * 2;
		period_elapsed = true;
	}
	snd_pcm_stream_unlock(smi2021->pcm_substream);

	if (period_elapsed)
		snd_pcm_period_elapsed(smi2021->pcm_substream);

}

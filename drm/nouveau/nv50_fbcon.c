/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#define NVIF_DEBUG_PRINT_DISABLE
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_fbcon.h"
#include "nouveau_vmm.h"

#include <nvif/push206e.h>

int
nv50_fbcon_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct nouveau_drm *drm = nouveau_drm(nfbdev->helper.dev);
	struct nouveau_channel *chan = drm->channel;
	int ret;

	ret = RING_SPACE(chan, rect->rop == ROP_COPY ? 7 : 11);
	if (ret)
		return ret;

	if (rect->rop != ROP_COPY) {
		BEGIN_NV04(chan, NvSub2D, 0x02ac, 1);
		OUT_RING(chan, 1);
	}
	BEGIN_NV04(chan, NvSub2D, 0x0588, 1);
	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR)
		OUT_RING(chan, ((uint32_t *)info->pseudo_palette)[rect->color]);
	else
		OUT_RING(chan, rect->color);
	BEGIN_NV04(chan, NvSub2D, 0x0600, 4);
	OUT_RING(chan, rect->dx);
	OUT_RING(chan, rect->dy);
	OUT_RING(chan, rect->dx + rect->width);
	OUT_RING(chan, rect->dy + rect->height);
	if (rect->rop != ROP_COPY) {
		BEGIN_NV04(chan, NvSub2D, 0x02ac, 1);
		OUT_RING(chan, 3);
	}
	FIRE_RING(chan);
	return 0;
}

int
nv50_fbcon_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct nouveau_drm *drm = nouveau_drm(nfbdev->helper.dev);
	struct nouveau_channel *chan = drm->channel;
	int ret;

	ret = RING_SPACE(chan, 12);
	if (ret)
		return ret;

	BEGIN_NV04(chan, NvSub2D, 0x0110, 1);
	OUT_RING(chan, 0);
	BEGIN_NV04(chan, NvSub2D, 0x08b0, 4);
	OUT_RING(chan, region->dx);
	OUT_RING(chan, region->dy);
	OUT_RING(chan, region->width);
	OUT_RING(chan, region->height);
	BEGIN_NV04(chan, NvSub2D, 0x08d0, 4);
	OUT_RING(chan, 0);
	OUT_RING(chan, region->sx);
	OUT_RING(chan, 0);
	OUT_RING(chan, region->sy);
	FIRE_RING(chan);
	return 0;
}

int
nv50_fbcon_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct nouveau_drm *drm = nouveau_drm(nfbdev->helper.dev);
	struct nouveau_channel *chan = drm->channel;
	uint32_t dwords, *data = (uint32_t *)image->data;
	uint32_t mask = ~(~0 >> (32 - info->var.bits_per_pixel));
	uint32_t *palette = info->pseudo_palette;
	int ret;

	if (image->depth != 1)
		return -ENODEV;

	ret = RING_SPACE(chan, 11);
	if (ret)
		return ret;

	BEGIN_NV04(chan, NvSub2D, 0x0814, 2);
	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		OUT_RING(chan, palette[image->bg_color] | mask);
		OUT_RING(chan, palette[image->fg_color] | mask);
	} else {
		OUT_RING(chan, image->bg_color);
		OUT_RING(chan, image->fg_color);
	}
	BEGIN_NV04(chan, NvSub2D, 0x0838, 2);
	OUT_RING(chan, image->width);
	OUT_RING(chan, image->height);
	BEGIN_NV04(chan, NvSub2D, 0x0850, 4);
	OUT_RING(chan, 0);
	OUT_RING(chan, image->dx);
	OUT_RING(chan, 0);
	OUT_RING(chan, image->dy);

	dwords = ALIGN(ALIGN(image->width, 8) * image->height, 32) >> 5;
	while (dwords) {
		int push = dwords > 2047 ? 2047 : dwords;

		ret = RING_SPACE(chan, push + 1);
		if (ret)
			return ret;

		dwords -= push;

		BEGIN_NI04(chan, NvSub2D, 0x0860, push);
		OUT_RINGp(chan, data, push);
		data += push;
	}

	FIRE_RING(chan);
	return 0;
}

int
nv50_fbcon_accel_init(struct fb_info *info)
{
	struct nouveau_fbdev *nfbdev = info->par;
	struct drm_device *dev = nfbdev->helper.dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_channel *chan = drm->channel;
	struct nvif_push *push = chan->chan.push;
	int ret, format;

	switch (info->var.bits_per_pixel) {
	case 8:
		format = 0xf3;
		break;
	case 15:
		format = 0xf8;
		break;
	case 16:
		format = 0xe8;
		break;
	case 32:
		switch (info->var.transp.length) {
		case 0: /* depth 24 */
		case 8: /* depth 32, just use 24.. */
			format = 0xe6;
			break;
		case 2: /* depth 30 */
			format = 0xd1;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	ret = nvif_object_ctor(&chan->user, "fbconTwoD", 0x502d, 0x502d,
			       NULL, 0, &nfbdev->twod);
	if (ret)
		return ret;

	ret = PUSH_WAIT(push, 56);
	if (ret) {
		nouveau_fbcon_gpu_lockup(info);
		return ret;
	}

	PUSH_NVSQ(push, NV502D, 0x0000, nfbdev->twod.handle);
	PUSH_NVSQ(push, NV502D, 0x0184, chan->vram.handle,
				0x0188, chan->vram.handle,
				0x018c, chan->vram.handle);

	PUSH_NVSQ(push, NV502D, 0x0200, format,
				0x0204, 1);
	PUSH_NVSQ(push, NV502D, 0x0214, info->fix.line_length,
				0x0218, info->var.xres_virtual,
				0x021c, info->var.yres_virtual,
				0x0220, upper_32_bits(nfbdev->vma->addr),
				0x0224, lower_32_bits(nfbdev->vma->addr));

	PUSH_NVSQ(push, NV502D, 0x0230, format,
				0x0234, 1);
	PUSH_NVSQ(push, NV502D, 0x0244, info->fix.line_length,
				0x0248, info->var.xres_virtual,
				0x024c, info->var.yres_virtual,
				0x0250, upper_32_bits(nfbdev->vma->addr),
				0x0254, lower_32_bits(nfbdev->vma->addr));

	PUSH_NVSQ(push, NV502D, 0x0290, 0);
	PUSH_NVSQ(push, NV502D, 0x02a0, 0x55);
	PUSH_NVSQ(push, NV502D, 0x02ac, 3);
	PUSH_NVSQ(push, NV502D, 0x02e8, 2,
				0x02ec, 1);

	PUSH_NVSQ(push, NV502D, 0X0580, 4,
				0x0584, format);

	PUSH_NVSQ(push, NV502D, 0x0800, 1,
				0x0804, format,
				0x0808, 0,
				0x080c, 0,
				0x0810, 1);
	PUSH_NVSQ(push, NV502D, 0x081c, 1);
	PUSH_NVSQ(push, NV502D, 0x0840, 0,
				0x0844, 1,
				0x0848, 0,
				0x084c, 1);

	PUSH_NVSQ(push, NV502D, 0x0888, 1);
	PUSH_NVSQ(push, NV502D, 0x08c0, 0,
				0x08c4, 1,
				0x08c8, 0,
				0x08cc, 1);

	PUSH_KICK(push);
	return 0;
}


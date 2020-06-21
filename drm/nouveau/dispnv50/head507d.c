/*
 * Copyright 2018 Red Hat Inc.
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
 */
#include "head.h"
#include "core.h"

#include <nvif/push507c.h>

#include <nvhw/class/cl507d.h>

int
head507d_procamp(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NV507D, 0x08a8 + (i * 0x400), asyh->procamp.sat.sin << 20 |
						      asyh->procamp.sat.cos << 8);
	return 0;
}

int
head507d_dither(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NV507D, 0x08a0 + (i * 0x400), asyh->dither.mode << 3 |
						      asyh->dither.bits << 1 |
						      asyh->dither.enable);
	return 0;
}

int
head507d_ovly(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	u32 bounds = 0;
	int ret;

	if (asyh->ovly.cpp) {
		switch (asyh->ovly.cpp) {
		case 4: bounds |= 0x00000300; break;
		case 2: bounds |= 0x00000100; break;
		default:
			WARN_ON(1);
			break;
		}
		bounds |= 0x00000001;
	} else {
		bounds |= 0x00000100;
	}

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NV507D, 0x0904 + (i * 0x400), bounds);
	return 0;
}

int
head507d_base(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	u32 bounds = 0;
	int ret;

	if (asyh->base.cpp) {
		switch (asyh->base.cpp) {
		case 8: bounds |= 0x00000500; break;
		case 4: bounds |= 0x00000300; break;
		case 2: bounds |= 0x00000100; break;
		case 1: bounds |= 0x00000000; break;
		default:
			WARN_ON(1);
			break;
		}
		bounds |= 0x00000001;
	}

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NV507D, 0x0900 + (i * 0x400), bounds);
	return 0;
}

static int
head507d_curs_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NV507D, 0x0880 + (i * 0x400), 0x05000000);
	return 0;
}

static int
head507d_curs_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 3)))
		return ret;

	PUSH_NVSQ(push, NV507D, 0x0880 + (i * 0x400), 0x80000000 |
						      asyh->curs.layout << 26 |
						      asyh->curs.format << 24,
				0x0884 + (i * 0x400), asyh->curs.offset >> 8);
	return 0;
}

int
head507d_curs_format(struct nv50_head *head, struct nv50_wndw_atom *asyw,
		     struct nv50_head_atom *asyh)
{
	switch (asyw->image.format) {
	case 0xcf: asyh->curs.format = 1; break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}
	return 0;
}

int
head507d_curs_layout(struct nv50_head *head, struct nv50_wndw_atom *asyw,
		     struct nv50_head_atom *asyh)
{
	switch (asyw->image.w) {
	case 32: asyh->curs.layout = 0; break;
	case 64: asyh->curs.layout = 1; break;
	default:
		return -EINVAL;
	}
	return 0;
}

int
head507d_core_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NV507D, 0x0874 + (i * 0x400), 0x00000000);
	return 0;
}

static int
head507d_core_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 9)))
		return ret;

	PUSH_NVSQ(push, NV507D, 0x0860 + (i * 0x400), asyh->core.offset >> 8);
	PUSH_NVSQ(push, NV507D, 0x0868 + (i * 0x400), asyh->core.h << 16 | asyh->core.w,
				0x086c + (i * 0x400), asyh->core.layout << 20 |
						     (asyh->core.pitch >> 8) << 8 |
						      asyh->core.blocks << 8 |
						      asyh->core.blockh,
				0x0870 + (i * 0x400), asyh->core.kind << 16 |
						      asyh->core.format << 8,
				0x0874 + (i * 0x400), asyh->core.handle);
	PUSH_NVSQ(push, NV507D, 0x08c0 + (i * 0x400), asyh->core.y << 16 | asyh->core.x);

	/* EVO will complain with INVALID_STATE if we have an
	 * active cursor and (re)specify HeadSetContextDmaIso
	 * without also updating HeadSetOffsetCursor.
	 */
	asyh->set.curs = asyh->curs.visible;
	asyh->set.olut = asyh->olut.handle != 0;
	return 0;
}

void
head507d_core_calc(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_disp *disp = nv50_disp(head->base.base.dev);
	if ((asyh->core.visible = (asyh->base.cpp != 0))) {
		asyh->core.x = asyh->base.x;
		asyh->core.y = asyh->base.y;
		asyh->core.w = asyh->base.w;
		asyh->core.h = asyh->base.h;
	} else
	if ((asyh->core.visible = (asyh->ovly.cpp != 0)) ||
	    (asyh->core.visible = asyh->curs.visible)) {
		/*XXX: We need to either find some way of having the
		 *     primary base layer appear black, while still
		 *     being able to display the other layers, or we
		 *     need to allocate a dummy black surface here.
		 */
		asyh->core.x = 0;
		asyh->core.y = 0;
		asyh->core.w = asyh->state.mode.hdisplay;
		asyh->core.h = asyh->state.mode.vdisplay;
	}
	asyh->core.handle = disp->core->chan.vram.handle;
	asyh->core.offset = 0;
	asyh->core.format = 0xcf;
	asyh->core.kind = 0;
	asyh->core.layout = 1;
	asyh->core.blockh = 0;
	asyh->core.blocks = 0;
	asyh->core.pitch = ALIGN(asyh->core.w, 64) * 4;
}

static int
head507d_olut_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV507D, HEAD_SET_BASE_LUT_LO(i),
		  NVDEF(NV507D, HEAD_SET_BASE_LUT_LO, ENABLE, DISABLE));
	return 0;
}

static int
head507d_olut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 3)))
		return ret;

	PUSH_MTHD(push, NV507D, HEAD_SET_BASE_LUT_LO(i),
		  NVDEF(NV507D, HEAD_SET_BASE_LUT_LO, ENABLE, ENABLE) |
		  NVVAL(NV507D, HEAD_SET_BASE_LUT_LO, MODE, asyh->olut.mode) |
		  NVVAL(NV507D, HEAD_SET_BASE_LUT_LO, ORIGIN, 0),

				HEAD_SET_BASE_LUT_HI(i),
		  NVVAL(NV507D, HEAD_SET_BASE_LUT_HI, ORIGIN, asyh->olut.offset >> 8));
	return 0;
}

static void
head507d_olut_load(struct drm_color_lut *in, int size, void __iomem *mem)
{
	for (; size--; in++, mem += 8) {
		writew(drm_color_lut_extract(in->  red, 11) << 3, mem + 0);
		writew(drm_color_lut_extract(in->green, 11) << 3, mem + 2);
		writew(drm_color_lut_extract(in-> blue, 11) << 3, mem + 4);
	}

	/* INTERPOLATE modes require a "next" entry to interpolate with,
	 * so we replicate the last entry to deal with this for now.
	 */
	writew(readw(mem - 8), mem + 0);
	writew(readw(mem - 6), mem + 2);
	writew(readw(mem - 4), mem + 4);
}

bool
head507d_olut(struct nv50_head *head, struct nv50_head_atom *asyh, int size)
{
	if (size != 256)
		return false;

	if (asyh->base.cpp == 1)
		asyh->olut.mode = NV507D_HEAD_SET_BASE_LUT_LO_MODE_LORES;
	else
		asyh->olut.mode = NV507D_HEAD_SET_BASE_LUT_LO_MODE_HIRES;

	asyh->olut.load = head507d_olut_load;
	return true;
}

int
head507d_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	struct nv50_head_mode *m = &asyh->mode;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 13)))
		return ret;

	PUSH_MTHD(push, NV507D, HEAD_SET_PIXEL_CLOCK(i),
		  NVVAL(NV507D, HEAD_SET_PIXEL_CLOCK, FREQUENCY, m->clock) |
		  NVDEF(NV507D, HEAD_SET_PIXEL_CLOCK, MODE, CLK_CUSTOM) |
		  NVDEF(NV507D, HEAD_SET_PIXEL_CLOCK, ADJ1000DIV1001, FALSE) |
		  NVDEF(NV507D, HEAD_SET_PIXEL_CLOCK, NOT_DRIVER, FALSE),

				HEAD_SET_CONTROL(i),
		  NVVAL(NV507D, HEAD_SET_CONTROL, STRUCTURE, m->interlace));

	PUSH_MTHD(push, NV507D, HEAD_SET_OVERSCAN_COLOR(i),
		  NVVAL(NV507D, HEAD_SET_OVERSCAN_COLOR, RED, 0) |
		  NVVAL(NV507D, HEAD_SET_OVERSCAN_COLOR, GRN, 0) |
		  NVVAL(NV507D, HEAD_SET_OVERSCAN_COLOR, BLU, 0),

				HEAD_SET_RASTER_SIZE(i),
		  NVVAL(NV507D, HEAD_SET_RASTER_SIZE, WIDTH, m->h.active) |
		  NVVAL(NV507D, HEAD_SET_RASTER_SIZE, HEIGHT, m->v.active),

				HEAD_SET_RASTER_SYNC_END(i),
		  NVVAL(NV507D, HEAD_SET_RASTER_SYNC_END, X, m->h.synce) |
		  NVVAL(NV507D, HEAD_SET_RASTER_SYNC_END, Y, m->v.synce),

				HEAD_SET_RASTER_BLANK_END(i),
		  NVVAL(NV507D, HEAD_SET_RASTER_BLANK_END, X, m->h.blanke) |
		  NVVAL(NV507D, HEAD_SET_RASTER_BLANK_END, Y, m->v.blanke),

				HEAD_SET_RASTER_BLANK_START(i),
		  NVVAL(NV507D, HEAD_SET_RASTER_BLANK_START, X, m->h.blanks) |
		  NVVAL(NV507D, HEAD_SET_RASTER_BLANK_START, Y, m->v.blanks),

				HEAD_SET_RASTER_VERT_BLANK2(i),
		  NVVAL(NV507D, HEAD_SET_RASTER_VERT_BLANK2, YSTART, m->v.blank2s) |
		  NVVAL(NV507D, HEAD_SET_RASTER_VERT_BLANK2, YEND, m->v.blank2e),

				HEAD_SET_RASTER_VERT_BLANK_DMI(i),
		  NVVAL(NV507D, HEAD_SET_RASTER_VERT_BLANK_DMI, DURATION, m->v.blankus));

	PUSH_MTHD(push, NV507D, HEAD_SET_DEFAULT_BASE_COLOR(i),
		  NVVAL(NV507D, HEAD_SET_DEFAULT_BASE_COLOR, RED, 0) |
		  NVVAL(NV507D, HEAD_SET_DEFAULT_BASE_COLOR, GREEN, 0) |
		  NVVAL(NV507D, HEAD_SET_DEFAULT_BASE_COLOR, BLUE, 0));
	return 0;
}

int
head507d_view(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 7)))
		return ret;

	PUSH_MTHD(push, NV507D, HEAD_SET_CONTROL_OUTPUT_SCALER(i),
		  NVDEF(NV507D, HEAD_SET_CONTROL_OUTPUT_SCALER, VERTICAL_TAPS, TAPS_1) |
		  NVDEF(NV507D, HEAD_SET_CONTROL_OUTPUT_SCALER, HORIZONTAL_TAPS, TAPS_1) |
		  NVVAL(NV507D, HEAD_SET_CONTROL_OUTPUT_SCALER, HRESPONSE_BIAS, 0) |
		  NVVAL(NV507D, HEAD_SET_CONTROL_OUTPUT_SCALER, VRESPONSE_BIAS, 0));

	PUSH_MTHD(push, NV507D, HEAD_SET_VIEWPORT_SIZE_IN(i),
		  NVVAL(NV507D, HEAD_SET_VIEWPORT_SIZE_IN, WIDTH, asyh->view.iW) |
		  NVVAL(NV507D, HEAD_SET_VIEWPORT_SIZE_IN, HEIGHT, asyh->view.iH));

	PUSH_MTHD(push, NV507D, HEAD_SET_VIEWPORT_SIZE_OUT(i),
		  NVVAL(NV507D, HEAD_SET_VIEWPORT_SIZE_OUT, WIDTH, asyh->view.oW) |
		  NVVAL(NV507D, HEAD_SET_VIEWPORT_SIZE_OUT, HEIGHT, asyh->view.oH),

				HEAD_SET_VIEWPORT_SIZE_OUT_MIN(i),
		  NVVAL(NV507D, HEAD_SET_VIEWPORT_SIZE_OUT_MIN, WIDTH, asyh->view.oW) |
		  NVVAL(NV507D, HEAD_SET_VIEWPORT_SIZE_OUT_MIN, HEIGHT, asyh->view.oH));
	return 0;
}

const struct nv50_head_func
head507d = {
	.view = head507d_view,
	.mode = head507d_mode,
	.olut = head507d_olut,
	.olut_size = 256,
	.olut_set = head507d_olut_set,
	.olut_clr = head507d_olut_clr,
	.core_calc = head507d_core_calc,
	.core_set = head507d_core_set,
	.core_clr = head507d_core_clr,
	.curs_layout = head507d_curs_layout,
	.curs_format = head507d_curs_format,
	.curs_set = head507d_curs_set,
	.curs_clr = head507d_curs_clr,
	.base = head507d_base,
	.ovly = head507d_ovly,
	.dither = head507d_dither,
	.procamp = head507d_procamp,
};

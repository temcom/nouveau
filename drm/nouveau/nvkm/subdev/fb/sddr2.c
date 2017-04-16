/*
 * Copyright 2014 Roy Spliet
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
 * Authors: Roy Spliet <rspliet@eclipso.eu>
 *          Ben Skeggs
 */
#include "priv.h"
#include "ram.h"

static const struct nvkm_ram_mr_xlat
ramddr2_cl[] = {
	{ 2, 2 }, { 3, 3 }, { 4, 4 }, { 5, 5 }, { 6, 6 },
	/* The following are available in some, but not all DDR2 docs */
	{ 7, 7 },
};

static const struct nvkm_ram_mr_xlat
ramddr2_wr[] = {
	{ 2, 1 }, { 3, 2 }, { 4, 3 }, { 5, 4 }, { 6, 5 },
	/* The following are available in some, but not all DDR2 docs */
	{ 7, 6 },
};

int
nvkm_sddr2_calc(struct nvkm_ram *ram)
{
	MR_ARGS(CL, WR, DLLoff, ODT);

	switch (ram->next->bios.timing_ver) {
	case 0x10:
		MR_LOAD(    CL, c->timing_10_CL);
		MR_LOAD(    WR, c->timing_10_WR);
		MR_LOAD(DLLoff, c->ramcfg_DLLoff);
		MR_COND(   ODT, c->timing_10_ODT, c->ramcfg_timing != 0xff);
		break;
	case 0x20:
		MR_LOAD(CL, (c->timing[1] & 0x0000001f) >> 0);
		MR_LOAD(WR, (c->timing[2] & 0x007f0000) >> 16);
		break;
	default:
		return -ENOSYS;
	}

	if (MR_XLAT(CL, ramddr2_cl) ||
	    MR_XLAT(WR, ramddr2_wr))
		return -EINVAL;

	MR_MASK(0, 0xe00, WR);
	MR_MASK(0, 0x070, CL);

	MR_MASK(1, 0x001, DLLoff);
	MR_BITS(1, 0x040, ODT, 0x2);
	MR_BITS(1, 0x004, ODT, 0x1);
	return 0;
}

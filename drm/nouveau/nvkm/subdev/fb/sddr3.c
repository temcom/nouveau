/*
 * Copyright 2013 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 * 	    Roy Spliet <rspliet@eclipso.eu>
 */
#include "priv.h"
#include "ram.h"

static const struct nvkm_ram_mr_xlat
ramddr3_cl[] = {
	{ 5, 2 }, { 6, 4 }, { 7, 6 }, { 8, 8 }, { 9, 10 }, { 10, 12 },
	{ 11, 14 },
	/* the below are mentioned in some, but not all, ddr3 docs */
	{ 12, 1 }, { 13, 3 }, { 14, 5 },
};

static const struct nvkm_ram_mr_xlat
ramddr3_wr[] = {
	{ 5, 1 }, { 6, 2 }, { 7, 3 }, { 8, 4 }, { 10, 5 }, { 12, 6 },
	/* the below are mentioned in some, but not all, ddr3 docs */
	{ 14, 7 }, { 15, 7 }, { 16, 0 },
};

static const struct nvkm_ram_mr_xlat
ramddr3_cwl[] = {
	{ 5, 0 }, { 6, 1 }, { 7, 2 }, { 8, 3 },
	/* the below are mentioned in some, but not all, ddr3 docs */
	{ 9, 4 }, { 10, 5 },
};

int
nvkm_sddr3_calc(struct nvkm_ram *ram)
{
	MR_ARGS(DLLoff, CWL, CL, WR, ODT);
	MR_LOAD(DLLoff, c->ramcfg_DLLoff);

	switch (ram->next->bios.timing_ver) {
	case 0x10:
		if (ram->next->bios.timing_hdr < 0x17) {
			/* XXX: NV50: Get CWL from the timing register */
			return -ENOSYS;
		}
		MR_LOAD(CWL, c->timing_10_CWL);
		MR_LOAD( CL, c->timing_10_CL);
		MR_LOAD( WR, c->timing_10_WR);
		MR_LOAD(ODT, c->timing_10_ODT);
		break;
	case 0x20:
		MR_LOAD(CWL, (c->timing[1] & 0x00000f80) >> 7);
		MR_LOAD( CL, (c->timing[1] & 0x0000001f) >> 0);
		MR_LOAD( WR, (c->timing[2] & 0x007f0000) >> 16);
		break;
	default:
		return -ENOSYS;
	}

	if (MR_XLAT(CWL, ramddr3_cwl) ||
	    MR_XLAT( CL, ramddr3_cl) ||
	    MR_XLAT( WR, ramddr3_wr))
		return -EINVAL;

	MR_MASK(0, 0xe00, WR);
	MR_BITS(0, 0x070, CL, 0xe);
	MR_BITS(0, 0x004, CL, 0x1);

	MR_MASK(1, 0x001, DLLoff);
	MR_BITS(1, 0x200, ODT, 0x1);
	MR_BITS(1, 0x040, ODT, 0x2);
	MR_BITS(1, 0x004, ODT, 0x4);

	MR_MASK(2, 0x038, CWL);
	return 0;
}

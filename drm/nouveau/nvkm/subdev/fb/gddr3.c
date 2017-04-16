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
#include "ram.h"

static const struct nvkm_ram_mr_xlat
ramgddr3_cl_lo[] = {
	{ 5, 5 }, { 7, 7 }, { 8, 0 }, { 9, 1 }, { 10, 2 }, { 11, 3 }, { 12, 8 },
	/* the below are mentioned in some, but not all, gddr3 docs */
	{ 13, 9 }, { 14, 6 },
	/* XXX: Per Samsung docs, are these used? They overlap with Qimonda */
	/* { 4, 4 }, { 5, 5 }, { 6, 6 }, { 12, 8 }, { 13, 9 }, { 14, 10 },
	 * { 15, 11 }, */
};

static const struct nvkm_ram_mr_xlat
ramgddr3_cl_hi[] = {
	{ 10, 2 }, { 11, 3 }, { 12, 4 }, { 13, 5 }, { 14, 6 }, { 15, 7 },
	{ 16, 0 }, { 17, 1 },
};

static const struct nvkm_ram_mr_xlat
ramgddr3_wr_lo[] = {
	{ 5, 2 }, { 7, 4 }, { 8, 5 }, { 9, 6 }, { 10, 7 },
	{ 11, 0 }, { 13 , 1 },
	/* the below are mentioned in some, but not all, gddr3 docs */
	{ 4, 0 }, { 6, 3 }, { 12, 1 },
};

int
nvkm_gddr3_calc(struct nvkm_ram *ram)
{
	MR_ARGS(CL, WR, CWL, DLLoff, ODT, RON);

	switch (ram->next->bios.timing_ver) {
	case 0x10:
		MR_LOAD(   CWL, c->timing_10_CWL);
		MR_LOAD(    CL, c->timing_10_CL);
		MR_LOAD(    WR, c->timing_10_WR);
		MR_LOAD(DLLoff, c->ramcfg_DLLoff);
		MR_COND(   ODT, c->timing_10_ODT, c->ramcfg_timing != 0xff);
		MR_LOAD(   RON, c->ramcfg_RON);
		break;
	case 0x20:
		MR_LOAD(CWL, (c->timing[1] & 0x00000f80) >> 7);
		MR_LOAD( CL, (c->timing[1] & 0x0000001f) >> 0);
		MR_LOAD( WR, (c->timing[2] & 0x007f0000) >> 16);
		break;
	default:
		return -ENOSYS;
	}

	if ((0 ?
		MR_XLAT(CL, ramgddr3_cl_hi) :
		MR_XLAT(CL, ramgddr3_cl_lo)) ||
	    MR_XLAT(WR, ramgddr3_wr_lo))
		return -EINVAL;

	MR_MASK(0, 0xe00, CWL);
	MR_BITS(0, 0x070, CL, 0x7);
	MR_BITS(0, 0x004, CL, 0x8);

	MR_BITS(1, 0x300, RON, 0x3);
	MR_BITS(1, 0x080, WR, 0x4);
	MR_MASK(1, 0x040, DLLoff);
	MR_BITS(1, 0x030, WR, 0x3);
	MR_BITS(1, 0x00c, ODT, 0x3);
	return 0;
}

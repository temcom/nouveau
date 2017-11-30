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
 */
#include "ram.h"

/* binary driver only executes this path if the condition (a) is true
 * for any configuration (combination of rammap+ramcfg+timing) that
 * can be reached on a given card.  for now, we will execute the branch
 * unconditionally in the hope that a "false everywhere" in the bios
 * tables doesn't actually mean "don't touch this".
 */
#define NOTE00(a) 1

int
nvkm_gddr5_calc(struct nvkm_ram *ram, bool nuts, int rq, int l3)
{
	MR_ARGS(rq, xd, pd, lf, vh, vr, vo, l3, WL, CL, WR, at[2], dt, ds);
	MR_LOAD(rq, rq);
	MR_LOAD(xd, !c->ramcfg_DLLoff);

	switch (ram->next->bios.ramcfg_ver) {
	case 0x10:
		MR_COND(l3, l3 > 1, l3);
		break;
	case 0x11:
		MR_LOAD(pd, c->ramcfg_11_01_80); /*XXX: RM !1->0 */
		MR_LOAD(lf, c->ramcfg_11_01_40);
		MR_LOAD(vh, c->ramcfg_11_02_10);
		MR_COND(vr, c->ramcfg_11_02_04, NOTE00(vr));
		MR_COND(vo, c->ramcfg_11_06, c->ramcfg_11_06);
		MR_LOAD(l3, !c->ramcfg_11_07_02);
		break;
	default:
		return -ENOSYS;
	}

	switch (ram->next->bios.timing_ver) {
	case 0x10:
		break;
	case 0x20:
		MR_LOAD(WL, (c->timing[1] & 0x00000f80) >> 7);
		MR_LOAD(CL, ((c->timing[1] & 0x0000001f) >>  0) - 5);
		MR_LOAD(WR, ((c->timing[2] & 0x007f0000) >> 16) - 4);
		MR_LOAD(at[0], c->timing_20_2e_c0);
		MR_LOAD(at[1], c->timing_20_2e_30);
		MR_LOAD(dt, c->timing_20_2e_03);
		MR_LOAD(ds, c->timing_20_2f_03);
		break;
	default:
		return -ENOSYS;
	}

	MR_BITS(0, 0xf00, WR, 0x0f);
	MR_BITS(0, 0x078, CL, 0x0f);
	MR_BITS(0, 0x007, WL, 0x07);

	MR_MASK(1, 0x080, xd);
	MR_MASK(1, 0x030, at[0]);
	MR_MASK(1, 0x00c, dt);
	MR_MASK(1, 0x003, ds);

	/* this seems wrong, alternate field used for the broadcast
	 * on nuts vs non-nuts configs..  meh, it matches for now.
	 */
	ram->mr1_nuts = ram->mr[1].data;
	if (nuts)
		MR_MASK(1, 0x030, at[1]);

	MR_MASK(3, 0x020, rq);

	MR_MASK(5, 0x004, l3);

	MR_MASK(6, 0xff0, vo);
	MR_MASK(6, 0x001, pd);

	MR_MASK(7, 0x300, vr);
	MR_MASK(7, 0x080, vh);
	MR_MASK(7, 0x008, lf);

	MR_BITS(8, 0x002, WR, 0x10);
	MR_BITS(8, 0x001, CL, 0x10);
	return 0;
}

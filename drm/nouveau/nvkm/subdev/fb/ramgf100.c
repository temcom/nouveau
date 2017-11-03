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
 * Authors: Ben Skeggs
 */
#include "ramgf100.h"

#include <core/option.h>
#include <subdev/bios/M0205.h>
#include <subdev/bios/rammap.h>
#include <subdev/bios/timing.h>
#include <subdev/clk.h>
#include <subdev/clk/pll.h>
#include <subdev/pmu.h>
#include <subdev/timer.h>
#include <engine/disp.h>
#include <engine/disp/head.h> /*XXX*/

static void
gf100_ram_calc_timing(struct gf100_ram *ram)
{
	struct nvkm_memx *memx = ram->memx;
	u32 mask, data;

	if (mask = 0, data = 0, ram->base.type == NVKM_RAM_TYPE_GDDR5) {
		data |= 0x00000011 * (ram->mode == DIV);
		mask |= 0x000000ff;
	}
	memx_mask(memx, 0x10f298, mask, data);
}

static void
gf100_ram_calc_train(struct gf100_ram *ram, u32 mask, u32 data)
{
	struct nvkm_memx *memx = ram->memx;
	int fbpa;

	memx_mask(memx, 0x10f910, mask, data);
	memx_mask(memx, 0x10f914, mask, data);

	if (data & 0x80000000) {
		for_each_set_bit(fbpa, &ram->base.fbpam, ram->base.fbpan) {
			const u32 addr = 0x110974 + (fbpa * 0x1000);
			memx_wait(memx, addr, 0x0000000f, 0x00000000, 500000);
		}
	}
}

static u8
gf100_ram_calc_fb_access(struct gf100_ram *ram, bool access, u8 r100b0c)
{
	struct nvkm_device *device = ram->base.fb->subdev.device;
	struct nvkm_disp *disp = device->disp;
	struct nvkm_memx *memx = ram->memx;
	struct nvkm_head *head;
	u32 heads = 0x00000000;

	if (nvkm_device_engine(device, NVKM_ENGINE_DISP)) {
		list_for_each_entry(head, &disp->head, head) {
			heads |= BIT(head->id);
		}
	}

	if (!access) {
		r100b0c = memx_mask(memx, 0x100b0c, 0x000000ff, r100b0c);
		if (heads) {
			nvkm_memx_wait_vblank(memx);
			memx_wr32(memx, 0x611200, 0x00001100 * heads);
		}
		nvkm_memx_block(memx);
	} else {
		nvkm_memx_unblock(memx);
		r100b0c = memx_mask(memx, 0x100b0c, 0x000000ff, r100b0c);
		if (heads)
			memx_wr32(memx, 0x611200, 0x00001110 * heads);
	}

	return r100b0c;
}

static int
gf100_ram_calc_gddr5(struct gf100_ram *ram)
{
	struct nvkm_device *device = ram->base.fb->subdev.device;
	struct nvkm_ram_data *c = ram->base.next;
	struct nvkm_ram_mr *mr = ram->base.mr;
	struct nvkm_memx *memx = ram->memx;
	u8 r100b0c;
	int ret;

	if ((ram->from == DIV && ram->mode != DIV && ram->mode != PLL2) ||
	    (ram->from != DIV && ram->mode != DIV))
		return -ENOSYS;

	ret = nvkm_gddr5_calc(&ram->base, false, ram->mode == DIV);
	if (ret)
		return ret;

	memx_mask(memx, 0x132100, 0x00000001, 0x00000001, FORCE);

	if (ram->mode != DIV) {
		if ( (nvkm_rd32(device, 0x10fe20) & 0x00000002) ||
		    !(nvkm_rd32(device, 0x137390) & 0x00010000)) {
			nvkm_mask(device, 0x10fe20, 0x00000002, 0x00000002);
			nvkm_mask(device, 0x10fe20, 0x00000002, 0x00000000);
			nvkm_msec(device, 2000,
				if (nvkm_rd32(device, 0x132020) & 0x00010000)
					break;
			);
		}

		memx_mask(memx, 0x10fe20, 0x00000005, 0x00000000, FORCE);
		memx_wr32(memx, 0x137320, 0x00000003);
		memx_wr32(memx, 0x137330, 0x81200006);
		memx_mask(memx, 0x10fe24, 0xffffffff, 0x0001160f);
		memx_mask(memx, 0x10fe20, 0x00000001, 0x00000001);
		memx_wait(memx, 0x137390, 0x00020000, 0x00020000, 64000);
		memx_mask(memx, 0x10fe20, 0x00000004, 0x00000004);

		memx_wr32(memx, 0x132004, 0x00011b0a);
		memx_mask(memx, 0x132000, 0x00000101, 0x00000101);
		memx_wait(memx, 0x137390, 0x00000002, 0x00000002, 64000);

		memx_mask(memx, 0x10fb04, 0x0000ffff, 0x00000000, FORCE);
		memx_mask(memx, 0x10fb08, 0x0000ffff, 0x00000000, FORCE);
		memx_wr32(memx, 0x10f988, 0x2004ff00);
		memx_wr32(memx, 0x10f98c, 0x003fc040);
		memx_wr32(memx, 0x10f990, 0x20012001);
		memx_wr32(memx, 0x10f998, 0x00011a00);
	} else {
		if (ram->from != DIV) {
			memx_wr32(memx, 0x137310, ram->dctl); /*XXX:sometimes?*/
			memx_wr32(memx, 0x137300, ram->dsrc);
		}
		memx_wr32(memx, 0x10f988, 0x20010000);
		memx_wr32(memx, 0x10f98c, 0x00000000);
		memx_wr32(memx, 0x10f990, 0x20012001);
		memx_wr32(memx, 0x10f998, 0x00010a00);
	}

	/* Wait for a vblank window, and disable FB access. */
	r100b0c = gf100_ram_calc_fb_access(ram, false, 0x12);

	memx_mask(memx, 0x10f200, 0x00000800, 0x00000000);
	if (ram->mode == DIV)
		memx_mask(memx, 0x10f824, 0x00000000, 0x00000000, FORCE);
	memx_wr32(memx, 0x10f210, 0x00000000);
	memx_nsec(memx, 1000);
	if (ram->from != DIV)
		gf100_ram_calc_train(ram, 0xffffffff, 0x000c1001);
	memx_wr32(memx, 0x10f310, 0x00000001);
	memx_nsec(memx, 1000);
	memx_wr32(memx, 0x10f090, 0x00000061);
	memx_wr32(memx, 0x10f090, 0xc000007f);
	memx_nsec(memx, 1000);

	if (ram->mode != DIV)
		memx_mask(memx, 0x10f824, 0xffffffff, 0x00007fd4);

	if (ram->from != DIV) {
		memx_mask(memx, 0x10f808, 0x00080000, 0x00000000);
		memx_mask(memx, 0x10f200, 0x00008000, 0x00008000);
		memx_mask(memx, 0x10f830, 0x41000000, 0x41000000);
		memx_mask(memx, 0x10f830, 0x01000000, 0x00000000);
		memx_mask(memx, 0x132100, 0x00000100, 0x00000100);
		memx_wr32(memx, 0x137310, ram->dctl); /*XXX:sometimes?*/
		memx_mask(memx, 0x10f050, 0x00000000, 0x00000000, FORCE);
		memx_mask(memx, 0x1373ec, 0x00003f3f, 0x00000f0f);
		memx_mask(memx, 0x1373f0, 0x00000001, 0x00000001);
	}

	if (ram->mode == DIV)
		memx_wr32(memx, 0x137310, ram->dctl & 0xf7ffffff);

	if (ram->from != DIV) {
		memx_mask(memx, 0x132100, 0x00000100, 0x00000000);
		nvkm_memx_fbpa_war_nsec(memx, 25000000 / c->freq + 1);
		memx_mask(memx, 0x10f830, 0x40700007, 0x00300007);
		memx_mask(memx, 0x1373f0, 0x00000002, 0x00000000);
		memx_mask(memx, 0x10f824, 0xfffff9ff, 0x00007877);
		memx_mask(memx, 0x132000, 0x00000100, 0x00000000);
	}

	if (ram->mode != DIV) {
		memx_mask(memx, 0x10f800, 0x000000ff, 0x00000000, FORCE);
		memx_mask(memx, 0x1373ec, 0x00003f3f, 0x00000000, FORCE);
		memx_mask(memx, 0x1373f0, 0x00000002, 0x00000002, FORCE);
		memx_wr32(memx, 0x10f830, 0x40700010);
		memx_mask(memx, 0x10f830, 0x00200000, 0x00000000);
		memx_mask(memx, 0x1373f8, 0x00002000, 0x00000000, FORCE);
		memx_mask(memx, 0x132100, 0x00000100, 0x00000100, FORCE);
		memx_mask(memx, 0x137310, 0x08000000, 0x08000000);
		memx_mask(memx, 0x10f050, 0x00000000, 0x00000000, FORCE);
		memx_mask(memx, 0x1373ec, 0x00030000, 0x00030000, FORCE);
		memx_mask(memx, 0x1373f0, 0x00000001, 0x00000000, FORCE);
		nvkm_memx_fbpa_war_nsec(memx, 25000000 / c->freq);
		memx_mask(memx, 0x132100, 0x00000100, 0x00000000);
		memx_mask(memx, 0x1373f8, 0x00002000, 0x00002000);
		memx_nsec(memx, 2000);
		memx_mask(memx, 0x10f808, 0x00080000, 0x00080000, FORCE);
		memx_mask(memx, 0x10f830, 0x40000000, 0x00000000, FORCE);
		memx_mask(memx, 0x10f200, 0x00008000, 0x00000000, FORCE);
	}

	memx_wr32(memx, 0x10f090, 0x4000007e);
	memx_nsec(memx, 2000);
	memx_wr32(memx, 0x10f314, 0x00000001);
	memx_wr32(memx, 0x10f210, 0x80000000);

	memx_mask(memx, 0x10f338, mr[3].mask, mr[3].data);
	memx_mask(memx, 0x10f300, mr[0].mask, mr[0].data, FORCE);
	memx_nsec(memx, 1000);

	gf100_ram_calc_timing(ram);

	if (ram->mode == DIV) {
		memx_mask(memx, 0x10f830, 0x00000000, 0x00000000, FORCE);
		gf100_ram_calc_train(ram, 0xffffffff, 0x80021001);
		gf100_ram_calc_train(ram, 0xffffffff, 0x80081001);
		memx_mask(memx, 0x10f830, 0x01000000, 0x01000000);
		memx_mask(memx, 0x10f830, 0x01000000, 0x00000000);
	} else {
		gf100_ram_calc_train(ram, 0xffffffff, 0x800e1008);
		memx_nsec(memx, 1000);
		memx_mask(memx, 0x10f800, 0x00000004, 0x00000004);
	}

	/* Re-enable FB access. */
	gf100_ram_calc_fb_access(ram, true, r100b0c);

	if (ram->mode != DIV) {
		memx_nsec(memx, 100000);
		memx_wr32(memx, 0x10f9b0, 0x05313f41);
		memx_wr32(memx, 0x10f9b4, 0x00002f50);
		gf100_ram_calc_train(ram, 0xffffffff, 0x010c1001);
	}

	memx_mask(memx, 0x10f200, 0x00000800, 0x00000800);
	return 0;
}

static void
gf100_ram_calc_sddr3_r10f830(struct gf100_ram *ram, bool on)
{
	memx_mask(ram->memx, 0x10f830, 0x00000006, 0x00000006 * on);
}

static void
gf100_ram_calc_sddr3_r137370(struct gf100_ram *ram, bool enable)
{
	memx_wr32(ram->memx, 0x137370, !enable ? 0x00000000 : ram->base.fbpam);
	memx_wr32(ram->memx, 0x137380, !enable ? 0x00000000 : 0x00000001);
}

static void
gf100_ram_calc_sddr3_r132018(struct gf100_ram *ram, unsigned lowspeed)
{
	u32 data = 0x00001000 | (lowspeed << 28) | ((ram->mode == DIV) << 15);
	u32 mask = 0x10009000;
	memx_mask(ram->memx, 0x132018, mask, data);
}

static void
gf100_ram_calc_sddr3_dll_reset(struct nvkm_memx *memx)
{
	if (!(memx_rd32(memx, 0x10f304) & 0x00000001)) {
		memx_mask(memx, 0x10f300, 0x00000100, 0x00000100, FORCE);
		memx_nsec(memx, 1000);
		memx_mask(memx, 0x10f300, 0x00000100, 0x00000000);
		memx_nsec(memx, 1000);
	}
}

static int
gf100_ram_calc_sddr3(struct gf100_ram *ram)
{
	struct nvkm_memx *memx = ram->memx;
	struct nvkm_ram_data *c = ram->base.next;
	unsigned locknsec = DIV_ROUND_UP(540000, c->freq) * 1000; /*XXX*/
	unsigned lowspeed = c->freq <= 750000; /*XXX: where's this from? */
	unsigned somefreq = 405000; /*XXX: where's this from? what is it? */
	u8 r100b0c;
	int ret;

	if ((ram->from == DIV && (ram->mode != DIV && ram->mode != PLL)) ||
	    (ram->from == PLL && (ram->mode != DIV && ram->mode != PLL)) ||
	    (ram->from != DIV && ram->from != PLL))
		return -ENOSYS;

	ret = nvkm_sddr3_calc(&ram->base);
	if (ret)
		return ret;

	/* Do some preparations for PLL-mode transitions early to reduce
	 * the amount of time we need to have FB access disabled.
	 */
	if (ram->from != DIV) {
		/* Prepare the divider-mode clock.
		 *
		 * If we're transitioning to another PLL mode, then
		 * this is an intermediate clock that's used during
		 * the MPLL update.
		 */
		memx_wr32(memx, 0x137310, ram->dctl);
		memx_wr32(memx, 0x137300, ram->dsrc);
	} else
	if (ram->mode != DIV) {
		/* Setup and enable MPLL. */
		memx_wr32(memx, 0x137320, 0x00000103);
		memx_wr32(memx, 0x137330, 0x81200606);
		memx_wr32(memx, 0x132004, 0x00051806);
		memx_mask(memx, 0x132000, 0x00000001, 0x00000001);
		memx_wait(memx, 0x137390, 0x00000002, 0x00000002, 64000);
		gf100_ram_calc_sddr3_r132018(ram, lowspeed);
	}

	if (ram->from != DIV || ram->mode != DIV)
		gf100_ram_calc_sddr3_r137370(ram, true);

	/* Wait for a vblank window, and disable FB access. */
	r100b0c = gf100_ram_calc_fb_access(ram, false, 0x12);

	memx_mask(memx, 0x10f200, 0x00000800, 0x00000000);
	memx_wr32(memx, 0x10f314, 0x00000001);
	memx_wr32(memx, 0x10f210, 0x00000000);
	memx_wr32(memx, 0x10f310, 0x00000001);
	memx_wr32(memx, 0x10f310, 0x00000001);
	memx_nsec(memx, 1000);
	memx_wr32(memx, 0x10f090, 0x00000060);
	memx_wr32(memx, 0x10f090, 0xc000007e);

	if (ram->from != DIV) {
		memx_mask(memx, 0x137360, 0x00000001, 0x00000001);
		if (!(memx_rd32(memx, 0x10f830) & 0x00000006))
			gf100_ram_calc_sddr3_r10f830(ram, c->freq < somefreq);
		gf100_ram_calc_sddr3_r137370(ram, false);
		memx_mask(memx, 0x132018, 0x00004000, 0x00000000);
		memx_mask(memx, 0x132000, 0x00000001, 0x00000000);
	}

	if (ram->from != DIV && ram->mode != DIV) {
		memx_wr32(memx, 0x137320, 0x00000103);
		memx_wr32(memx, 0x137330, 0x81200606);
		memx_wr32(memx, 0x132004, 0x00062406);
		memx_mask(memx, 0x132000, 0x00000001, 0x00000001);
		memx_wait(memx, 0x137390, 0x00000002, 0x00000002, 64000);
	}

	if (ram->from == DIV && ram->mode == DIV)
		memx_wr32(memx, 0x137310, ram->dctl);

	memx_mask(memx, 0x10f874, 0x04000000, lowspeed << 26);

	gf100_ram_calc_sddr3_r132018(ram, lowspeed);
	memx_wr32(memx, 0x10f660, 0x00001010);
	memx_mask(memx, 0x10f824, 0x00006000, 0x00006000);

	if (ram->mode != DIV) {
		gf100_ram_calc_sddr3_r10f830(ram, c->freq < somefreq);
		gf100_ram_calc_sddr3_r137370(ram, true);
		memx_mask(memx, 0x137360, 0x00000001, 0x00000000);
	}

	memx_wr32(memx, 0x10f090, 0x4000007f);
	memx_wr32(memx, 0x10f210, 0x80000000);
	memx_nsec(memx, locknsec);
	gf100_ram_calc_sddr3_dll_reset(memx);
	memx_mask(memx, 0x10f300, 0x00000000, 0x00000000, FORCE);
	memx_nsec(memx, 1000);
	memx_mask(memx, 0x10f808, 0x10000020, 0x00000000);
	gf100_ram_calc_sddr3_dll_reset(memx);
	memx_nsec(memx, locknsec);
	memx_mask(memx, 0x10f830, 0x01000000, 0x01000000);
	memx_mask(memx, 0x10f830, 0x01000000, 0x00000000);

	/* Re-enable FB access. */
	gf100_ram_calc_fb_access(ram, true, r100b0c);

	memx_mask(memx, 0x10f200, 0x00000800, 0x00000800);
	return 0;
}

static int
gf100_ram_calc_src(struct nvkm_device *device, u32 rsrc, u32 rctl,
		   u32 khz, u32 *dsrc, u32 *dctl)
{
	struct nvkm_clk *clk = device->clk;
	int ref, div;

	/* Configure divider to use SPPLLx as its reference clock. */
	*dctl  = nvkm_rd32(device, rctl);
	*dsrc  = nvkm_rd32(device, rsrc);
	*dsrc |= 0x00000003;

	/* Determine input frequency. */
	if (!(*dsrc & 0x00000100))
		ref = nvkm_clk_read(clk, nv_clk_src_sppll0);
	else
		ref = nvkm_clk_read(clk, nv_clk_src_sppll1);
	if (ref < 0)
		return ref;

	/* Calculate divider that'll get us to the target clock. */
	div = ((ref * 2) / khz) - 2;
	if (div < 0 || div > 63)
		return -EINVAL;

	if (*dsrc & 0x00000100)
		*dctl = (*dctl & ~0x00003f00) | div << 8;
	else
		*dctl = (*dctl & ~0x0000003f) | div;
	return (ref * 2) / (div + 2);
}

static int
gf100_ram_calc_pll(struct gf100_ram *ram)
{
	int mode;

	if (ram->base.type == NVKM_RAM_TYPE_GDDR5)
		mode = PLL2;
	else
		mode = PLL;

	ram->mode = mode;
	return 324000;
}

static int
gf100_ram_calc_div(struct gf100_ram *ram)
{
	struct nvkm_device *device = ram->base.fb->subdev.device;
	struct nvkm_ram_data *c = ram->base.next;
	int khz;

	khz = gf100_ram_calc_src(device, 0x137300, 0x137310, c->freq,
				 &ram->dsrc, &ram->dctl);
	if (khz < 0)
		return khz;

	ram->mode = DIV;
	return khz;
}

static int
gf100_ram_calc_xits(struct gf100_ram *ram, u8 flags)
{
	struct nvkm_device *device = ram->base.fb->subdev.device;
	u32 khz = nvkm_clk_read(device->clk, nv_clk_src_mem);
	int ret;

	/* Prepare for script generation. */
	ret = nvkm_memx_init(device->pmu, &ram->memx);
	if (ret)
		return ret;
	ram->mode = INVALID;

	/* Determine current clock mode. */
	if (nvkm_rd32(device, 0x137360) & 0x00000002) {
		if (nvkm_rd32(device, 0x137360) & 0x00000001)
			ram->from = DIV;
		else
			ram->from = PLL;
	} else {
		if (nvkm_rd32(device, 0x1373f0) & 0x00000002)
			ram->from = PLL2;
		else
			ram->from = DIV;
	}

	nvkm_memx_fbpa_war_nsec(ram->memx, 25000000 / khz + 1);

	/* Determine target clock mode, and coefficients. */
	if (!(flags & NVKM_CLK_NO_DIV))
		gf100_ram_calc_div(ram);
	if (!(flags & NVKM_CLK_NO_PLL) && ram->mode == INVALID)
		gf100_ram_calc_pll(ram);
	if (ram->mode == INVALID)
		return -ENOSYS;

	/* Reset MPLL, if it's not currently in use. */
	if ( (nvkm_rd32(device, 0x132000) & 0x00000002) ||
	    !(nvkm_rd32(device, 0x137390) & 0x00000001)) {
		nvkm_mask(device, 0x132000, 0x00000002, 0x00000002);
		nvkm_mask(device, 0x132000, 0x00000002, 0x00000000);
		nvkm_msec(device, 2000,
			if (nvkm_rd32(device, 0x132000) & 0x00010000)
				break;
		);
	}

	/* The remainder of the process depends on RAM Type. */
	switch (ram->base.type) {
	case NVKM_RAM_TYPE_DDR3:
		ret = gf100_ram_calc_sddr3(ram);
		if (ret)
			return ret;
		break;
	case NVKM_RAM_TYPE_GDDR5:
		ret = gf100_ram_calc_gddr5(ram);
		if (ret)
			return ret;
		break;
	default:
		return -ENOSYS;
	}

	return 0;
}

int
gf100_ram_calc(struct nvkm_ram *base, u8 flags, u32 freq)
{
	struct gf100_ram *ram = gf100_ram(base);
	int ret;

	ret = nvkm_ram_data(&ram->base, freq, &ram->base.target);
	if (ret)
		return ret;

	ram->base.next = &ram->base.target;

	return gf100_ram_calc_xits(ram, flags);
}

void
gf100_ram_prog_10f4xx(struct gf100_ram *ram, u32 khz)
{
	struct nvkm_device *device = ram->base.fb->subdev.device;
	struct nvkm_ram_data c;
	u32 mask = 0, data = 0;
	int ret;

	ret = nvkm_ram_data(&ram->base, khz, &c);
	if (ret)
		return;

	nvkm_mask(device, 0x10f468, mask, data);
	nvkm_mask(device, 0x10f420, mask, data);
	nvkm_mask(device, 0x10f430, mask, data);
	nvkm_mask(device, 0x10f400, mask, data);
	nvkm_mask(device, 0x10f410, mask, data);
	nvkm_mask(device, 0x10f444, mask, data);
}

int
gf100_ram_prog(struct nvkm_ram *base)
{
	struct gf100_ram *ram = gf100_ram(base);
	struct nvkm_device *device = ram->base.fb->subdev.device;

	if (!nvkm_boolopt(device->cfgopt, "NvMemExec", true))
		return 0;

	gf100_ram_prog_10f4xx(ram, 1000);

	nvkm_memx_fini(&ram->memx, true);

	if (ram->base.type != NVKM_RAM_TYPE_GDDR5) {
		nvkm_mask(device, 0x132018, 0x00000000, 0x00000000);
		nvkm_mask(device, 0x10f824, 0x00000000, 0x00000000);
	}

	nvkm_mask(device, 0x10f824, 0x00000000, 0x00000000);
	nvkm_mask(device, 0x10f808, 0x00000000, 0x00000000);

	if (ram->mode == DIV) {
		if (ram->base.type != NVKM_RAM_TYPE_GDDR5) {
			nvkm_wr32(device, 0x137370, 0x00000000);
			nvkm_wr32(device, 0x137380, 0x00000000);
		}
		nvkm_mask(device, 0x132000, 0x00000001, 0x00000000);
	}

	gf100_ram_prog_10f4xx(ram, ram->base.next->freq);
	return 0;
}

void
gf100_ram_tidy(struct nvkm_ram *base)
{
	struct gf100_ram *ram = gf100_ram(base);
	nvkm_memx_fini(&ram->memx, false);
}

int
gf100_ram_train_init_0(struct nvkm_ram *ram, struct gt215_ram_train *train)
{
	struct nvkm_subdev *subdev = &ram->fb->subdev;
	struct nvkm_device *device = subdev->device;
	int i, j;

	static const u8  train0[] = {
		0x00, 0xff, 0x55, 0xaa, 0x33, 0xcc,
		0x00, 0xff, 0xff, 0x00, 0xff, 0x00,
	};

	static const u32 train1[] = {
		0x00000000, 0xffffffff,
		0x55555555, 0xaaaaaaaa,
		0x33333333, 0xcccccccc,
		0xf0f0f0f0, 0x0f0f0f0f,
		0x00ff00ff, 0xff00ff00,
		0x0000ffff, 0xffff0000,
	};

	if ((train->mask & 0x03c3) == 0x03c3) {
		for (i = 0; i < 0x30; i++) {
			for (j = 0; j < 8; j += 4) {
				nvkm_wr32(device, 0x10f968 + j, (i << 8));
				nvkm_wr32(device, 0x10f920 + j, 0x00000000 |
						train->type08.data[i] << 4 |
						train->type06.data[i]);
				nvkm_wr32(device, 0x10f918 + j,
						train->type00.data[i]);
				nvkm_wr32(device, 0x10f920 + j, 0x00000100 |
						train->type09.data[i] << 4 |
						train->type07.data[i]);
				nvkm_wr32(device, 0x10f918 + j,
						train->type01.data[i]);
			}
		}
	} else {
		nvkm_debug(subdev,
			"missing link training data, using defaults\n");

		for (i = 0; i < 0x30; i++) {
			for (j = 0; j < 8; j += 4) {
				nvkm_wr32(device, 0x10f968 + j, (i << 8));
				nvkm_wr32(device, 0x10f920 + j, 0x00000100 |
								train0[i % 12]);
				nvkm_wr32(device, 0x10f918 + j, train1[i % 12]);
				nvkm_wr32(device, 0x10f920 + j, 0x00000000 |
								train0[i % 12]);
				nvkm_wr32(device, 0x10f918 + j, train1[i % 12]);
			}
		}
	}

	if (train->mask & 0x10) {
		for (j = 0; j < 8; j += 4) {
			for (i = 0; i < 0x100; i++) {
				nvkm_wr32(device, 0x10f968 + j, i);
				nvkm_wr32(device, 0x10f900 + j,
						train->type04.data[i]);
			}
		}
	}

	return 0;
}

int
gf100_ram_train_init(struct nvkm_ram *ram)
{
	struct nvkm_device *device = ram->fb->subdev.device;
	u8 ramcfg = nvbios_ramcfg_index(&ram->fb->subdev);
	struct gt215_ram_train *train;
	int ret, i;

	if (!(train = kzalloc(sizeof(*train), GFP_KERNEL)))
		return -ENOMEM;

	for (i = 0; i < 0x100; i++) {
		ret = gt215_ram_train_type(ram, i, ramcfg, train);
		if (ret && ret != -ENOENT)
			break;
	}

	switch (ram->type) {
	case NVKM_RAM_TYPE_GDDR5:
		nvkm_mask(device, 0x137360, 0x00000002, 0x00000000);
		ret = gf100_ram_train_init_0(ram, train);
		break;
	default:
		ret = 0;
		break;
	}

	kfree(train);
	return ret;
}

int
gf100_ram_init(struct nvkm_ram *base)
{
	nvkm_mask(base->fb->subdev.device, 0x10f160, 0x00000010, 0x00000010);
	return gf100_ram_train_init(base);
}

u32
gf100_ram_probe_fbpa_amount(struct nvkm_device *device, int fbpa)
{
	return nvkm_rd32(device, 0x11020c + (fbpa * 0x1000));
}

int
gf100_ram_probe_fbp_ltcs(struct nvkm_device *device, int fbp)
{
	return 1;
}

int
gf100_ram_probe_fbps(struct nvkm_device *device, unsigned long *opt)
{
	*opt = nvkm_rd32(device, 0x022554);
	return nvkm_rd32(device, 0x022438);
}

int
gf100_ram_ctor(const struct nvkm_ram_func *func, struct nvkm_fb *fb,
	       struct nvkm_ram *ram)
{
	struct nvkm_subdev *subdev = &fb->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_bios *bios = device->bios;
	const u32 rsvd_head = ( 256 * 1024); /* vga memory */
	const u32 rsvd_tail = (1024 * 1024); /* vbios etc */
	enum nvkm_ram_type type = nvkm_fb_bios_memtype(bios);
	u64 total = 0, lcomm = ~0, lower, ubase, usize;
	int ret, fbp, ltcn = 0;

	ram->fbpn = func->probe_fbps(device, &ram->fbpm);
	ram->fbpm = ((1 << ram->fbpn) - 1) & ~ram->fbpm;
	ram->fbps = hweight32(ram->fbpm);
	nvkm_debug(subdev, "%d FBP (s): %d present (%lx)\n",
		   ram->fbpn, ram->fbps, ram->fbpm);

	ram->fbpan = func->probe_fbpas(device, &ram->fbpam);
	ram->fbpam = ((1 << ram->fbpan) - 1) & ~ram->fbpam;
	ram->fbpas = hweight32(ram->fbpam);
	nvkm_debug(subdev, "%d FBPA(s): %d present (%lx)\n",
		   ram->fbpan, ram->fbpas, ram->fbpam);

	for_each_set_bit(fbp, &ram->fbpm, ram->fbpn) {
		int ltcs = func->probe_fbp_ltcs(device, fbp);
		u32 size = 0, fbpa_fbp = (ram->fbpan / ram->fbpn);
		u32 fbpa = fbp * fbpa_fbp;

		while (fbpa_fbp--) {
			if (ram->fbpam & BIT(fbpa))
				size += func->probe_fbpa_amount(device, fbpa);
			fbpa++;
		}

		nvkm_debug(subdev, "FBP %d: %4d MiB, %d LTC(s)\n",
			   fbp, size, ltcs);
		lcomm  = min(lcomm, (u64)(size / ltcs) << 20);
		total += (u64) size << 20;
		ltcn  += ltcs;
	}

	lower = lcomm * ltcn;
	ubase = lcomm + func->upper;
	usize = total - lower;

	nvkm_debug(subdev, "Lower: %4lld MiB @ %010llx\n", lower >> 20, 0ULL);
	nvkm_debug(subdev, "Upper: %4lld MiB @ %010llx\n", usize >> 20, ubase);
	nvkm_debug(subdev, "Total: %4lld MiB\n", total >> 20);

	ret = nvkm_ram_ctor(func, fb, type, total, ram);
	if (ret)
		return ret;

	nvkm_mm_fini(&ram->vram);

	/* Some GPUs are in what's known as a "mixed memory" configuration.
	 *
	 * This is either where some FBPs have more memory than the others,
	 * or where LTCs have been disabled on a FBP.
	 */
	if (lower != total) {
		/* The common memory amount is addressed normally. */
		ret = nvkm_mm_init(&ram->vram, NVKM_RAM_MM_NORMAL,
				   rsvd_head >> NVKM_RAM_MM_SHIFT,
				   (lower - rsvd_head) >> NVKM_RAM_MM_SHIFT, 1);
		if (ret)
			return ret;

		/* And the rest is much higher in the physical address
		 * space, and may not be usable for certain operations.
		 */
		ret = nvkm_mm_init(&ram->vram, NVKM_RAM_MM_MIXED,
				   ubase >> NVKM_RAM_MM_SHIFT,
				   (usize - rsvd_tail) >> NVKM_RAM_MM_SHIFT, 1);
		if (ret)
			return ret;
	} else {
		/* GPUs without mixed-memory are a lot nicer... */
		ret = nvkm_mm_init(&ram->vram, NVKM_RAM_MM_NORMAL,
				   rsvd_head >> NVKM_RAM_MM_SHIFT,
				   (total - rsvd_head - rsvd_tail) >>
				   NVKM_RAM_MM_SHIFT, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int
gf100_ram_new_data(struct gf100_ram *ram, u8 ramcfg, int i)
{
	struct nvkm_bios *bios = ram->base.fb->subdev.device->bios;
	struct nvkm_ram_data *cfg;
	struct nvbios_ramcfg *v = &ram->base.diff;
	struct nvbios_ramcfg *c;
	u8  ver, hdr, cnt, len;
	u32 data;
	int ret;

	if (!(cfg = kmalloc(sizeof(*cfg), GFP_KERNEL)))
		return -ENOMEM;
	c = &cfg->bios;

	/* memory config data for a range of target frequencies */
	data = nvbios_rammapEp(bios, i, &ver, &hdr, &cnt, &len, &cfg->bios);
	if (ret = -ENOENT, !data)
		goto done;
	if (ret = -ENOSYS, ver != 0x10 || hdr < 0x0e)
		goto done;
	if (ret = 1, !cfg->bios.rammap_min && !cfg->bios.rammap_max)
		goto done;

	/* ... and a portion specific to the attached memory */
	data = nvbios_rammapSp(bios, data, ver, hdr, cnt, len, ramcfg,
			       &ver, &hdr, &cfg->bios);
	if (ret = -EINVAL, !data)
		goto done;
	if (ret = -ENOSYS, ver != 0x10 || hdr < 0x0d)
		goto done;

	/* lookup memory timings, if bios says they're present */
	if (cfg->bios.ramcfg_timing != 0xff) {
		data = nvbios_timingEp(bios, cfg->bios.ramcfg_timing,
				       &ver, &hdr, &cnt, &len,
				       &cfg->bios);
		if (ret = -EINVAL, !data)
			goto done;
		if (ret = -ENOSYS, ver != 0x10 || hdr < 0x19)
			goto done;
	}

	list_add_tail(&cfg->head, &ram->base.cfg);
	ret = 0;

	(void)v;
	(void)c;
done:
	if (ret)
		kfree(cfg);
	return ret;
}

int
gf100_ram_new_(const struct nvkm_ram_func *func,
	       struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	struct nvkm_subdev *subdev = &fb->subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct gf100_ram *ram;
	int ret, i;

	if (!(ram = kzalloc(sizeof(*ram), GFP_KERNEL)))
		return -ENOMEM;
	*pram = &ram->base;

	ret = gf100_ram_ctor(func, fb, &ram->base);
	if (ret)
		return ret;

	/* parse bios data for all rammap table entries up-front, and
	 * build information on whether certain fields differ between
	 * any of the entries.
	 *
	 * the binary driver appears to completely ignore some fields
	 * when all entries contain the same value.  at first, it was
	 * hoped that these were mere optimisations and the bios init
	 * tables had configured as per the values here, but there is
	 * evidence now to suggest that this isn't the case and we do
	 * need to treat this condition as a "don't touch" indicator.
	 */
	for (i = 0; ret >= 0; i++) {
		ret = gf100_ram_new_data(ram, nvbios_ramcfg_index(subdev), i);
		if (ret < 0 && ret != -ENOENT) {
			nvkm_error(subdev, "failed to parse ramcfg data\n");
			return ret;
		}
	}

	ret = nvbios_pll_parse(bios, 0x0c, &ram->refpll);
	if (ret) {
		nvkm_error(subdev, "mclk refpll data not found\n");
		return ret;
	}

	ret = nvbios_pll_parse(bios, 0x04, &ram->mempll);
	if (ret) {
		nvkm_error(subdev, "mclk pll data not found\n");
		return ret;
	}

	return 0;
}

static const struct nvkm_ram_func
gf100_ram = {
	.upper = 0x0200000000,
	.probe_fbps = gf100_ram_probe_fbps,
	.probe_fbp_ltcs = gf100_ram_probe_fbp_ltcs,
	.probe_fbpas = gf100_ram_probe_fbps,
	.probe_fbpa_amount = gf100_ram_probe_fbpa_amount,
	.init = gf100_ram_init,
	.calc = gf100_ram_calc,
	.prog = gf100_ram_prog,
	.tidy = gf100_ram_tidy,
};

int
gf100_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	return gf100_ram_new_(&gf100_ram, fb, pram);
}

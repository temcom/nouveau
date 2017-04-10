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

static void
gf100_ram_train(struct gf100_ramfuc *fuc, u32 magic)
{
	struct gf100_ram *ram = container_of(fuc, typeof(*ram), fuc);
	struct nvkm_fb *fb = ram->base.fb;
	struct nvkm_device *device = fb->subdev.device;
	u32 part = nvkm_rd32(device, 0x022438), i;
	u32 mask = nvkm_rd32(device, 0x022554);
	u32 addr = 0x110974;

	ram_wr32(fuc, 0x10f910, magic);
	ram_wr32(fuc, 0x10f914, magic);

	for (i = 0; (magic & 0x80000000) && i < part; addr += 0x1000, i++) {
		if (mask & (1 << i))
			continue;
		ram_wait(fuc, addr, 0x0000000f, 0x00000000, 500000);
	}
}

int
gf100_ram_calc(struct nvkm_ram *base, u8 flags, u32 freq)
{
	struct gf100_ram *ram = gf100_ram(base);
	struct gf100_ramfuc *fuc = &ram->fuc;
	struct nvkm_subdev *subdev = &ram->base.fb->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_clk *clk = device->clk;
	struct nvkm_bios *bios = device->bios;
	struct nvbios_ramcfg cfg;
	u8  ver, cnt, len, strap;
	struct {
		u32 data;
		u8  size;
	} rammap, ramcfg, timing;
	int ref, div, out;
	int from, mode;
	int N1, M1, P;
	int ret;

	/* lookup memory config data relevant to the target frequency */
	rammap.data = nvbios_rammapEm(bios, freq / 1000, &ver, &rammap.size,
				      &cnt, &ramcfg.size, &cfg);
	if (!rammap.data || ver != 0x10 || rammap.size < 0x0e) {
		nvkm_error(subdev, "invalid/missing rammap entry\n");
		return -EINVAL;
	}

	/* locate specific data set for the attached memory */
	strap = nvbios_ramcfg_index(subdev);
	if (strap >= cnt) {
		nvkm_error(subdev, "invalid ramcfg strap\n");
		return -EINVAL;
	}

	ramcfg.data = rammap.data + rammap.size + (strap * ramcfg.size);
	if (!ramcfg.data || ver != 0x10 || ramcfg.size < 0x0e) {
		nvkm_error(subdev, "invalid/missing ramcfg entry\n");
		return -EINVAL;
	}

	/* lookup memory timings, if bios says they're present */
	strap = nvbios_rd08(bios, ramcfg.data + 0x01);
	if (strap != 0xff) {
		timing.data = nvbios_timingEe(bios, strap, &ver, &timing.size,
					      &cnt, &len);
		if (!timing.data || ver != 0x10 || timing.size < 0x19) {
			nvkm_error(subdev, "invalid/missing timing entry\n");
			return -EINVAL;
		}
	} else {
		timing.data = 0;
	}

	ret = ram_init(fuc, ram->base.fb);
	if (ret)
		return ret;

	/* determine current mclk configuration */
	from = !!(ram_rd32(fuc, 0x1373f0) & 0x00000002); /*XXX: ok? */

	/* determine target mclk configuration */
	if (!(ram_rd32(fuc, 0x137300) & 0x00000100))
		ref = nvkm_clk_read(clk, nv_clk_src_sppll0);
	else
		ref = nvkm_clk_read(clk, nv_clk_src_sppll1);
	div = max(min((ref * 2) / freq, (u32)65), (u32)2) - 2;
	out = (ref * 2) / (div + 2);
	mode = freq != out;

	ram_mask(fuc, 0x137360, 0x00000002, 0x00000000);

	if ((ram_rd32(fuc, 0x132000) & 0x00000002) || 0 /*XXX*/) {
		ram_nuke(fuc, 0x132000);
		ram_mask(fuc, 0x132000, 0x00000002, 0x00000002);
		ram_mask(fuc, 0x132000, 0x00000002, 0x00000000);
	}

	if (mode == 1) {
		ram_nuke(fuc, 0x10fe20);
		ram_mask(fuc, 0x10fe20, 0x00000002, 0x00000002);
		ram_mask(fuc, 0x10fe20, 0x00000002, 0x00000000);
	}

// 0x00020034 // 0x0000000a
	ram_wr32(fuc, 0x132100, 0x00000001);

	if (mode == 1 && from == 0) {
		/* calculate refpll */
		ret = gt215_pll_calc(subdev, &ram->refpll, ram->mempll.refclk,
				     &N1, NULL, &M1, &P);
		if (ret <= 0) {
			nvkm_error(subdev, "unable to calc refpll\n");
			return ret ? ret : -ERANGE;
		}

		ram_wr32(fuc, 0x10fe20, 0x20010000);
		ram_wr32(fuc, 0x137320, 0x00000003);
		ram_wr32(fuc, 0x137330, 0x81200006);
		ram_wr32(fuc, 0x10fe24, (P << 16) | (N1 << 8) | M1);
		ram_wr32(fuc, 0x10fe20, 0x20010001);
		ram_wait(fuc, 0x137390, 0x00020000, 0x00020000, 64000);

		/* calculate mempll */
		ret = gt215_pll_calc(subdev, &ram->mempll, freq,
				     &N1, NULL, &M1, &P);
		if (ret <= 0) {
			nvkm_error(subdev, "unable to calc refpll\n");
			return ret ? ret : -ERANGE;
		}

		ram_wr32(fuc, 0x10fe20, 0x20010005);
		ram_wr32(fuc, 0x132004, (P << 16) | (N1 << 8) | M1);
		ram_wr32(fuc, 0x132000, 0x18010101);
		ram_wait(fuc, 0x137390, 0x00000002, 0x00000002, 64000);
	} else
	if (mode == 0) {
		ram_wr32(fuc, 0x137300, 0x00000003);
	}

	if (from == 0) {
		ram_nuke(fuc, 0x10fb04);
		ram_mask(fuc, 0x10fb04, 0x0000ffff, 0x00000000);
		ram_nuke(fuc, 0x10fb08);
		ram_mask(fuc, 0x10fb08, 0x0000ffff, 0x00000000);
		ram_wr32(fuc, 0x10f988, 0x2004ff00);
		ram_wr32(fuc, 0x10f98c, 0x003fc040);
		ram_wr32(fuc, 0x10f990, 0x20012001);
		ram_wr32(fuc, 0x10f998, 0x00011a00);
		ram_wr32(fuc, 0x13d8f4, 0x00000000);
	} else {
		ram_wr32(fuc, 0x10f988, 0x20010000);
		ram_wr32(fuc, 0x10f98c, 0x00000000);
		ram_wr32(fuc, 0x10f990, 0x20012001);
		ram_wr32(fuc, 0x10f998, 0x00010a00);
	}

	if (from == 0) {
// 0x00020039 // 0x000000ba
	}

// 0x0002003a // 0x00000002
	ram_wr32(fuc, 0x100b0c, 0x00080012);
// 0x00030014 // 0x00000000 // 0x02b5f070
// 0x00030014 // 0x00010000 // 0x02b5f070
	ram_wr32(fuc, 0x611200, 0x00003300);
// 0x00020034 // 0x0000000a
// 0x00030020 // 0x00000001 // 0x00000000

	ram_mask(fuc, 0x10f200, 0x00000800, 0x00000000);
	ram_wr32(fuc, 0x10f210, 0x00000000);
	ram_nsec(fuc, 1000);
	if (mode == 0)
		gf100_ram_train(fuc, 0x000c1001);
	ram_wr32(fuc, 0x10f310, 0x00000001);
	ram_nsec(fuc, 1000);
	ram_wr32(fuc, 0x10f090, 0x00000061);
	ram_wr32(fuc, 0x10f090, 0xc000007f);
	ram_nsec(fuc, 1000);

	if (from == 0) {
		ram_wr32(fuc, 0x10f824, 0x00007fd4);
	} else {
		ram_wr32(fuc, 0x1373ec, 0x00020404);
	}

	if (mode == 0) {
		ram_mask(fuc, 0x10f808, 0x00080000, 0x00000000);
		ram_mask(fuc, 0x10f200, 0x00008000, 0x00008000);
		ram_wr32(fuc, 0x10f830, 0x41500010);
		ram_mask(fuc, 0x10f830, 0x01000000, 0x00000000);
		ram_mask(fuc, 0x132100, 0x00000100, 0x00000100);
		ram_wr32(fuc, 0x10f050, 0xff000090);
		ram_wr32(fuc, 0x1373ec, 0x00020f0f);
		ram_wr32(fuc, 0x1373f0, 0x00000003);
		ram_wr32(fuc, 0x137310, 0x81201616);
		ram_wr32(fuc, 0x132100, 0x00000001);
// 0x00020039 // 0x000000ba
		ram_wr32(fuc, 0x10f830, 0x00300017);
		ram_wr32(fuc, 0x1373f0, 0x00000001);
		ram_wr32(fuc, 0x10f824, 0x00007e77);
		ram_wr32(fuc, 0x132000, 0x18030001);
		ram_wr32(fuc, 0x10f090, 0x4000007e);
		ram_nsec(fuc, 2000);
		ram_wr32(fuc, 0x10f314, 0x00000001);
		ram_wr32(fuc, 0x10f210, 0x80000000);
		ram_wr32(fuc, 0x10f338, 0x00300220);
		ram_wr32(fuc, 0x10f300, 0x0000011d);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f290, 0x02060505);
		ram_wr32(fuc, 0x10f294, 0x34208288);
		ram_wr32(fuc, 0x10f298, 0x44050411);
		ram_wr32(fuc, 0x10f29c, 0x0000114c);
		ram_wr32(fuc, 0x10f2a0, 0x42e10069);
		ram_wr32(fuc, 0x10f614, 0x40044f77);
		ram_wr32(fuc, 0x10f610, 0x40044f77);
		ram_wr32(fuc, 0x10f344, 0x00600009);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f348, 0x00700008);
		ram_wr32(fuc, 0x61c140, 0x19240000);
		ram_wr32(fuc, 0x10f830, 0x00300017);
		gf100_ram_train(fuc, 0x80021001);
		gf100_ram_train(fuc, 0x80081001);
		ram_wr32(fuc, 0x10f340, 0x00500004);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f830, 0x01300017);
		ram_wr32(fuc, 0x10f830, 0x00300017);
// 0x00030020 // 0x00000000 // 0x00000000
// 0x00020034 // 0x0000000b
		ram_wr32(fuc, 0x100b0c, 0x00080028);
		ram_wr32(fuc, 0x611200, 0x00003330);
	} else {
		ram_wr32(fuc, 0x10f800, 0x00001800);
		ram_wr32(fuc, 0x13d8f4, 0x00000000);
		ram_wr32(fuc, 0x1373ec, 0x00020404);
		ram_wr32(fuc, 0x1373f0, 0x00000003);
		ram_wr32(fuc, 0x10f830, 0x40700010);
		ram_wr32(fuc, 0x10f830, 0x40500010);
		ram_wr32(fuc, 0x13d8f4, 0x00000000);
		ram_wr32(fuc, 0x1373f8, 0x00000000);
		ram_wr32(fuc, 0x132100, 0x00000101);
		ram_wr32(fuc, 0x137310, 0x89201616);
		ram_wr32(fuc, 0x10f050, 0xff000090);
		ram_wr32(fuc, 0x1373ec, 0x00030404);
		ram_wr32(fuc, 0x1373f0, 0x00000002);
	// 0x00020039 // 0x00000011
		ram_wr32(fuc, 0x132100, 0x00000001);
		ram_wr32(fuc, 0x1373f8, 0x00002000);
		ram_nsec(fuc, 2000);
		ram_wr32(fuc, 0x10f808, 0x7aaa0050);
		ram_wr32(fuc, 0x10f830, 0x00500010);
		ram_wr32(fuc, 0x10f200, 0x00ce1000);
		ram_wr32(fuc, 0x10f090, 0x4000007e);
		ram_nsec(fuc, 2000);
		ram_wr32(fuc, 0x10f314, 0x00000001);
		ram_wr32(fuc, 0x10f210, 0x80000000);
		ram_wr32(fuc, 0x10f338, 0x00300200);
		ram_wr32(fuc, 0x10f300, 0x0000084d);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f290, 0x0b343825);
		ram_wr32(fuc, 0x10f294, 0x3483028e);
		ram_wr32(fuc, 0x10f298, 0x440c0600);
		ram_wr32(fuc, 0x10f29c, 0x0000214c);
		ram_wr32(fuc, 0x10f2a0, 0x42e20069);
		ram_wr32(fuc, 0x10f200, 0x00ce0000);
		ram_wr32(fuc, 0x10f614, 0x60044e77);
		ram_wr32(fuc, 0x10f610, 0x60044e77);
		ram_wr32(fuc, 0x10f340, 0x00500000);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f344, 0x00600228);
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f348, 0x00700000);
		ram_wr32(fuc, 0x13d8f4, 0x00000000);
		ram_wr32(fuc, 0x61c140, 0x09a40000);

		gf100_ram_train(fuc, 0x800e1008);

		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f800, 0x00001804);
	// 0x00030020 // 0x00000000 // 0x00000000
	// 0x00020034 // 0x0000000b
		ram_wr32(fuc, 0x13d8f4, 0x00000000);
		ram_wr32(fuc, 0x100b0c, 0x00080028);
		ram_wr32(fuc, 0x611200, 0x00003330);
		ram_nsec(fuc, 100000);
		ram_wr32(fuc, 0x10f9b0, 0x05313f41);
		ram_wr32(fuc, 0x10f9b4, 0x00002f50);

		gf100_ram_train(fuc, 0x010c1001);
	}

	ram_mask(fuc, 0x10f200, 0x00000800, 0x00000800);
// 0x00020016 // 0x00000000

	if (mode == 0)
		ram_mask(fuc, 0x132000, 0x00000001, 0x00000000);

	return 0;
}

int
gf100_ram_prog(struct nvkm_ram *base)
{
	struct gf100_ram *ram = gf100_ram(base);
	struct nvkm_device *device = ram->base.fb->subdev.device;
	ram_exec(&ram->fuc, nvkm_boolopt(device->cfgopt, "NvMemExec", true));
	return 0;
}

void
gf100_ram_tidy(struct nvkm_ram *base)
{
	struct gf100_ram *ram = gf100_ram(base);
	ram_exec(&ram->fuc, false);
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
	struct nvbios_ramcfg *p, *c;
	u8  ver, hdr, cnt, len;
	u32 data;
	int ret;

	if (!(cfg = kmalloc(sizeof(*cfg), GFP_KERNEL)))
		return -ENOMEM;

	if (!list_empty(&ram->base.cfg))
		p = &list_last_entry(&ram->base.cfg, typeof(*cfg), head)->bios;
	else
		p = &cfg->bios;
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
	(void)p;
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

	ram->fuc.r_0x10fe20 = ramfuc_reg(0x10fe20);
	ram->fuc.r_0x10fe24 = ramfuc_reg(0x10fe24);
	ram->fuc.r_0x137320 = ramfuc_reg(0x137320);
	ram->fuc.r_0x137330 = ramfuc_reg(0x137330);

	ram->fuc.r_0x132000 = ramfuc_reg(0x132000);
	ram->fuc.r_0x132004 = ramfuc_reg(0x132004);
	ram->fuc.r_0x132100 = ramfuc_reg(0x132100);

	ram->fuc.r_0x137390 = ramfuc_reg(0x137390);

	ram->fuc.r_0x10f290 = ramfuc_reg(0x10f290);
	ram->fuc.r_0x10f294 = ramfuc_reg(0x10f294);
	ram->fuc.r_0x10f298 = ramfuc_reg(0x10f298);
	ram->fuc.r_0x10f29c = ramfuc_reg(0x10f29c);
	ram->fuc.r_0x10f2a0 = ramfuc_reg(0x10f2a0);

	ram->fuc.r_0x10f300 = ramfuc_reg(0x10f300);
	ram->fuc.r_0x10f338 = ramfuc_reg(0x10f338);
	ram->fuc.r_0x10f340 = ramfuc_reg(0x10f340);
	ram->fuc.r_0x10f344 = ramfuc_reg(0x10f344);
	ram->fuc.r_0x10f348 = ramfuc_reg(0x10f348);

	ram->fuc.r_0x10f910 = ramfuc_reg(0x10f910);
	ram->fuc.r_0x10f914 = ramfuc_reg(0x10f914);

	ram->fuc.r_0x100b0c = ramfuc_reg(0x100b0c);
	ram->fuc.r_0x10f050 = ramfuc_reg(0x10f050);
	ram->fuc.r_0x10f090 = ramfuc_reg(0x10f090);
	ram->fuc.r_0x10f200 = ramfuc_reg(0x10f200);
	ram->fuc.r_0x10f210 = ramfuc_reg(0x10f210);
	ram->fuc.r_0x10f310 = ramfuc_reg(0x10f310);
	ram->fuc.r_0x10f314 = ramfuc_reg(0x10f314);
	ram->fuc.r_0x10f610 = ramfuc_reg(0x10f610);
	ram->fuc.r_0x10f614 = ramfuc_reg(0x10f614);
	ram->fuc.r_0x10f800 = ramfuc_reg(0x10f800);
	ram->fuc.r_0x10f808 = ramfuc_reg(0x10f808);
	ram->fuc.r_0x10f824 = ramfuc_reg(0x10f824);
	ram->fuc.r_0x10f830 = ramfuc_reg(0x10f830);
	ram->fuc.r_0x10f988 = ramfuc_reg(0x10f988);
	ram->fuc.r_0x10f98c = ramfuc_reg(0x10f98c);
	ram->fuc.r_0x10f990 = ramfuc_reg(0x10f990);
	ram->fuc.r_0x10f998 = ramfuc_reg(0x10f998);
	ram->fuc.r_0x10f9b0 = ramfuc_reg(0x10f9b0);
	ram->fuc.r_0x10f9b4 = ramfuc_reg(0x10f9b4);
	ram->fuc.r_0x10fb04 = ramfuc_reg(0x10fb04);
	ram->fuc.r_0x10fb08 = ramfuc_reg(0x10fb08);
	ram->fuc.r_0x137310 = ramfuc_reg(0x137300);
	ram->fuc.r_0x137310 = ramfuc_reg(0x137310);
	ram->fuc.r_0x137360 = ramfuc_reg(0x137360);
	ram->fuc.r_0x1373ec = ramfuc_reg(0x1373ec);
	ram->fuc.r_0x1373f0 = ramfuc_reg(0x1373f0);
	ram->fuc.r_0x1373f8 = ramfuc_reg(0x1373f8);

	ram->fuc.r_0x61c140 = ramfuc_reg(0x61c140);
	ram->fuc.r_0x611200 = ramfuc_reg(0x611200);

	ram->fuc.r_0x13d8f4 = ramfuc_reg(0x13d8f4);
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

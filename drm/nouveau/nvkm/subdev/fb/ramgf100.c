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
#include <subdev/bios/perf.h>
#include <subdev/bios/M0205.h>
#include <subdev/bios/rammap.h>
#include <subdev/bios/timing.h>
#include <subdev/clk.h>
#include <subdev/clk/pll.h>
#include <subdev/gpio.h>
#include <subdev/pmu.h>
#include <subdev/timer.h>
#include <engine/disp.h>
#include <engine/disp/head.h> /*XXX*/

void
gf100_ram_calc_r61c140(struct nvkm_ram *base)
{
	struct gf100_ram *ram = gf100_ram(base);
	struct nvbios_ramcfg *v = &ram->base.diff;
	struct nvkm_ram_data *c = ram->base.next;
	struct nvkm_memx *memx = ram->memx;
	u32 mask = 0, data = 0;

	if (v->ramcfg_10_02_20) {
		data |= 0x08000000 * !!c->bios.ramcfg_10_02_20;
		mask |= 0x08000000;
	}
	if (v->ramcfg_10_02_01) {
		data |= 0x10000000 * !!c->bios.ramcfg_10_02_01;
		mask |= 0x10000000;
	}
	if (v->ramcfg_10_04_03) {
		data |= c->bios.ramcfg_10_04_03 << 25;
		mask |= 0x06000000;
	}
	memx_mask(memx, 0x61c140, mask, data);
}

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

static void
gf100_ram_calc_gddr5_r10f640(struct gf100_ram *ram)
{
	struct nvkm_ram_data *c = ram->base.next;
	struct nvkm_bios *bios = ram->base.fb->subdev.device->bios;
	struct nvkm_memx *memx = ram->memx;
	u32 addr, data[2];
	int fbpa;

	for_each_set_bit(fbpa, &ram->base.fbpam, ram->base.fbpan) {
		addr = 0x110000 + (fbpa * 0x1000);
		if (c->bios.rammap_10_04_40) {
			if (nvbios_perfSSp(bios, addr + 0x640, &data[0]) ||
			    nvbios_perfSSp(bios, addr + 0x644, &data[1]))
				break;

			memx_mask(memx, addr + 0x644, 0xffffffff, data[1]);
			memx_mask(memx, addr + 0x640, 0xffffffff, data[0]);
		} else {
			data[0] = 0x11111111 * c->bios.ramcfg_10_07_f0;
			data[1] = 0x11111111 * c->bios.ramcfg_10_07_0f;
			if (memx_rd32(memx, addr + 0x644) != data[1] ||
			    memx_rd32(memx, addr + 0x640) != data[0]) {
				memx_wr32(memx, 0x10f644, data[1]);
				memx_wr32(memx, 0x10f640, data[0]);
				break;
			}
		}
	}
}

static int
gf100_ram_calc_gddr5(struct gf100_ram *ram)
{
	struct nvkm_device *device = ram->base.fb->subdev.device;
	struct nvkm_gpio *gpio = device->gpio;
	struct nvbios_ramcfg *v = &ram->base.diff;
	struct nvkm_ram_data *c = ram->base.next;
	struct nvkm_ram_mr *mr = ram->base.mr;
	struct nvkm_memx *memx = ram->memx;
	bool r100750 = false;
	u32 mask, data, diff, nsec = 0;
	u8 r100b0c;
	int ret, i;

	if ((ram->from == DIV && ram->mode != DIV && ram->mode != PLL2) ||
	    (ram->from != DIV && ram->mode != DIV))
		return -ENOSYS;

	ret = nvkm_gddr5_calc(&ram->base, false, ram->mode == DIV,
			      ram->M0203.lp3 ? (ram->mode == DIV) ? 2 : 1 : 0);
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
		memx_wr32(memx, 0x137320, ram->rsrc);
		memx_wr32(memx, 0x137330, ram->rctl);
		memx_mask(memx, 0x10fe24, 0xffffffff, ram->rpll);
		memx_mask(memx, 0x10fe20, 0x00000001, 0x00000001);
		memx_wait(memx, 0x137390, 0x00020000, 0x00020000, 64000);
		memx_mask(memx, 0x10fe20, 0x00000004, 0x00000004);

		memx_wr32(memx, 0x132004, ram->mpll);
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

	if (v->ramcfg_10_04_10 && !c->bios.ramcfg_10_04_10 &&
	    nvkm_gpio_get(gpio, 0, 0x2e, DCB_GPIO_UNUSED) == 0) {
		nvkm_gpio_set(gpio, 0, 0x2e, DCB_GPIO_UNUSED, 1, &memx->sink);
		memx_nsec(memx, 20000);
	}

	memx_mask(memx, 0x10f200, 0x00000800, 0x00000000);

	if (v->ramcfg_10_02_10 && !c->bios.ramcfg_10_02_10) {
		memx_mask(memx, 0x10f808, 0x40000000, 0x40000000, FORCE);
		if (ram->mode == DIV)
			memx_mask(memx, 0x10f824, 0x00000180, 0x00000000);
	} else
	if (v->ramcfg_10_02_10) {
		memx_mask(memx, 0x10f824, 0x00000600, 0x00000000, FORCE);
	}

	memx_wr32(memx, 0x10f210, 0x00000000);
	memx_nsec(memx, 1000);
	if (ram->from != DIV)
		gf100_ram_calc_train(ram, 0xffffffff, 0x000c1001);
	memx_wr32(memx, 0x10f310, 0x00000001);
	memx_nsec(memx, 1000);
	memx_wr32(memx, 0x10f090, 0x00000061);
	memx_wr32(memx, 0x10f090, 0xc000007f);
	memx_nsec(memx, 1000);

	if (v->ramcfg_10_04_08 && c->bios.ramcfg_10_04_08 ==
	    nvkm_gpio_get(gpio, 0, 0x18, DCB_GPIO_UNUSED)) {
		nvkm_gpio_set(gpio, 0, 0x18, DCB_GPIO_UNUSED,
			      !c->bios.ramcfg_10_04_08,
			      &memx->sink);
		if (c->bios.rammap_fbvddq_usec)
			nsec += c->bios.rammap_fbvddq_usec * 1000;
		else
			nsec += 64000;
	}

	if (ram->mode == DIV) {
		data = 0x00020000 * !c->bios.ramcfg_10_02_10;
		mask = 0x00030000;
		memx_mask(memx, 0x1373ec, mask, data);
	} else {
		memx_mask(memx, 0x10f824, 0xffffffff, 0x00007fd4);
	}

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

	if (nsec)
		memx_nsec(memx, nsec);

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

	if (v->ramcfg_10_02_08) {
		data = 0x00001000 * !c->bios.ramcfg_10_02_08;
		mask = 0x00001000;
		memx_mask(memx, 0x10f200, mask, data);
	}

	if (mask = 0, data = 0, v->ramcfg_10_02_20) {
		data |= 0xf0000000 * !!c->bios.ramcfg_10_02_20;
		mask |= 0xf0000000;
	}
	if (v->ramcfg_10_02_04) {
		data |= 0x01000000 * !c->bios.ramcfg_10_02_04;
		mask |= 0x01000000;
	}
	if (v->ramcfg_10_04_03) {
		data |= c->bios.ramcfg_10_04_03 << 8;
		mask |= 0x00000300;
	}
	diff = memx_mask(memx, 0x10f604, mask, data, DIFF);
	if (diff & 0xf0000000)
		r100750 = true;

	if (mask = 0, data = 0, v->ramcfg_10_02_01) {
		data |= 0x00000100 * !!c->bios.ramcfg_10_02_01;
		mask |= 0x00000100;
	}
	memx_mask(memx, 0x10f614, mask, data);

	if (mask = 0, data = 0, v->ramcfg_10_02_02) {
		data |= 0x00000100 * !!c->bios.ramcfg_10_02_02;
		mask |= 0x00000100;
	}
	memx_mask(memx, 0x10f610, mask, data);

	if (v->ramcfg_10_02_04) {
		data = 0x32a00000 * !c->bios.ramcfg_10_02_04;
		mask = 0x32a00000;
		memx_mask(memx, 0x10f808, mask, data);
	}

	if (ram->base.func->calc_r1373f8)
		ram->base.func->calc_r1373f8(&ram->base);

	if (v->ramcfg_10_03_0f) {
		data = 0x11111111 * c->bios.ramcfg_10_03_0f;
		mask = 0xffffffff;
		memx_mask(memx, 0x10f618, mask, data);
	}

	if (memx_mask(memx, 0x10f330, mr[1].mask, mr[1].data, DIFF))
		memx_nsec(memx, 1000);
	if (memx_mask(memx, 0x10f340, mr[5].mask, mr[5].data & ~0x004, DIFF))
		memx_nsec(memx, 1000);
	if (memx_mask(memx, 0x10f344, mr[6].mask, mr[6].data, DIFF))
		memx_nsec(memx, 1000);
	memx_mask(memx, 0x10f348, mr[7].mask, mr[7].data);

	if (r100750) {
		memx_mask(memx, 0x100750, 0x00000008, 0x00000008);
		memx_wr32(memx, 0x100710, 0x00000000);
		memx_wait(memx, 0x100710, 0x80000000, 0x80000000, 200000);
	}

	ram->base.func->calc_r61c140_100c00(&ram->base);

	gf100_ram_calc_gddr5_r10f640(ram);

	if (v->ramcfg_10_04_10 && c->bios.ramcfg_10_04_10 &&
	    nvkm_gpio_get(gpio, 0, 0x2e, DCB_GPIO_UNUSED) == 1) {
		nvkm_gpio_set(gpio, 0, 0x2e, DCB_GPIO_UNUSED, 0, &memx->sink);
		memx_nsec(memx, 20000);
	}

	if (ram->mode == DIV)
		memx_mask(memx, 0x10f830, 0x00000000, 0x00000000, FORCE);

	if (c->bios.rammap_10_0d_01) {
		data = 0x11111111 * c->bios.ramcfg_10_05_f0;
		memx_mask(memx, 0x10f628, 0xffffffff, data);
		data = 0x11111111 * c->bios.ramcfg_10_05_0f;
		memx_mask(memx, 0x10f62c, 0xffffffff, data);
	}

	if (ram->mode == DIV) {
		gf100_ram_calc_train(ram, 0xffffffff, 0x80021001);
		if (c->bios.rammap_10_0d_02) {
			memx_nsec(memx, 1000);
			data = 0x11111111 * c->bios.ramcfg_10_06_f0;
			memx_wr32(memx, 0x10f630, data);
			data = 0x11111111 * c->bios.ramcfg_10_06_0f;
			memx_wr32(memx, 0x10f634, data);
			data = c->bios.ramcfg_10_0a;
			for (i = 0; i < 8; i++)
				memx_wr32(memx, 0x10fc20 + (i * 0x0c), data);
			memx_nsec(memx, 1000);
		} else {
			gf100_ram_calc_train(ram, 0xffffffff, 0x80081001);
		}

		if (memx_mask(memx, 0x10f340, mr[5].mask, mr[5].data, DIFF))
			memx_nsec(memx, 1000);
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

	if (v->rammap_10_04_02 && c->bios.rammap_10_04_02)
		memx_mask(memx, 0x10f200, 0x00000800, 0x00000800);

	return 0;
}

static void
gf100_ram_calc_sddr3_r10f658(struct gf100_ram *ram)
{
	struct nvkm_ram_data *c = ram->base.next;
	struct nvkm_memx *memx = ram->memx;
	memx_mask(memx, 0x10f658, 0x00ffffff,
			(c->bios.ramcfg_10_06_f0 << 20) |
			(c->bios.ramcfg_10_06_0f << 16) |
			(c->bios.ramcfg_10_05_f0 << 12) |
			(c->bios.ramcfg_10_05_0f << 8) |
			(c->bios.ramcfg_10_05_f0 << 4) |
			(c->bios.ramcfg_10_05_0f << 0));
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
gf100_ram_calc_sddr3_r132018(struct gf100_ram *ram, unsigned lowspeed,
			     unsigned rammap_10_04_01, bool mask14)
{
	u32 data = 0x00001000 | (lowspeed << 28) | ((ram->mode == DIV) << 15);
	u32 mask = 0x10009000;
	if (rammap_10_04_01 || mask14) {
		data |= 0x00004000 * rammap_10_04_01;
		mask |= 0x00004000;
	}
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
	struct nvkm_gpio *gpio = ram->base.fb->subdev.device->gpio;
	struct nvbios_ramcfg *v = &ram->base.diff;
	struct nvkm_ram_data *c = ram->base.next;
	struct nvkm_ram_mr *mr = ram->base.mr;
	struct nvkm_memx *memx = ram->memx;
	unsigned locknsec = DIV_ROUND_UP(540000, c->freq) * 1000; /*XXX*/
	unsigned lowspeed = c->freq <= 750000; /*XXX: where's this from? */
	unsigned somefreq = 405000; /*XXX: where's this from? what is it? */
	unsigned rammap_10_04_01 = !v->rammap_10_04_01 || c->bios.rammap_10_04_01;
	unsigned r10f200_11;
	u32 mask, data, nsec;
	u8 r100b0c;
	int ret, fbpa;

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
		memx_wr32(memx, 0x137320, ram->rsrc);
		memx_wr32(memx, 0x137330, ram->rctl);
		memx_wr32(memx, 0x132004, ram->mpll);
		memx_mask(memx, 0x132000, 0x00000001, 0x00000001);
		memx_wait(memx, 0x137390, 0x00000002, 0x00000002, 64000);
		gf100_ram_calc_sddr3_r132018(ram, lowspeed, rammap_10_04_01, false);
	}

	if (ram->from != DIV || ram->mode != DIV)
		gf100_ram_calc_sddr3_r137370(ram, true);

	if (v->ramcfg_10_02_10) {
		mask = 0x00006000 * !!rammap_10_04_01;
		data = 0x00000000;
		if (mask = 0, data = 0, !c->bios.ramcfg_10_02_10) {
			memx_mask(memx, 0x10f808, 0x40000000, 0x40000000);
			mask |= 0x00000180;
		} else {
			mask |= 0x00000600;
		}
		memx_mask(memx, 0x10f824, mask, data);
	}

	/* Wait for a vblank window, and disable FB access. */
	r100b0c = gf100_ram_calc_fb_access(ram, false, 0x12);

	r10f200_11 = memx_mask(memx, 0x10f200, 0x00000800, 0x00000000, DIFF);
	if (v->rammap_10_04_02)
		r10f200_11 = c->bios.rammap_10_04_02;

	if (!(memx_rd32(memx, 0x10f304) & 0x001) && (mr[1].data & 0x001)) {
		memx_wr32(memx, 0x10f314, 0x00000001);
		memx_mask(memx, 0x10f304, 0x00000001, 0x00000001);
		memx_nsec(memx, 1000);
	}

	if (v->ramcfg_10_02_10 && !c->bios.ramcfg_10_02_10) {
		memx_mask(memx, 0x10f808, 0x04000000, 0x04000000);
		memx_mask(memx, 0x1373ec, 0x00030000, 0x00020000);
	}

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

	if (v->ramcfg_10_04_08 && c->bios.ramcfg_10_04_08 ==
	    nvkm_gpio_get(gpio, 0, 0x18, DCB_GPIO_UNUSED)) {
		nvkm_gpio_set(gpio, 0, 0x18, DCB_GPIO_UNUSED,
			      !c->bios.ramcfg_10_04_08,
			      &memx->sink);
		/*XXX: later! */
		nsec = c->bios.rammap_fbvddq_usec * 1000;
		if (!nsec)
			nsec = 64000;
		memx_nsec(memx, nsec);
	}

	if (ram->from != DIV && ram->mode != DIV) {
		memx_wr32(memx, 0x137320, ram->rsrc);
		memx_wr32(memx, 0x137330, ram->rctl);
		memx_wr32(memx, 0x132004, ram->mpll);
		memx_mask(memx, 0x132000, 0x00000001, 0x00000001);
		memx_wait(memx, 0x137390, 0x00000002, 0x00000002, 64000);
	}

	if (ram->from == DIV && ram->mode == DIV)
		memx_wr32(memx, 0x137310, ram->dctl);

	memx_mask(memx, 0x10f874, 0x04000000, lowspeed << 26);

	if (rammap_10_04_01)
		memx_mask(memx, 0x10f824, 0x00006000, 0x00000000);
	else
		gf100_ram_calc_sddr3_r10f658(ram);

	gf100_ram_calc_sddr3_r132018(ram, lowspeed, rammap_10_04_01, true);
	if (rammap_10_04_01)
		memx_nsec(memx, 20000);

	if (v->rammap_10_04_08) {
		if (c->bios.rammap_10_04_08) {
			data = c->bios.ramcfg_10_08    << 8 |
			       c->bios.ramcfg_10_07_f0 << 4 |
			       c->bios.ramcfg_10_07_0f;
			mask = 0x0000ffff;
			memx_mask(memx, 0x10f660, mask, data, FORCE);

			if (mask = 0, data = 0, v->ramcfg_10_03_0f) {
				data |= c->bios.ramcfg_10_03_0f << 16;
				mask |= 0x000f0000;
			}
			if (v->ramcfg_10_09_f0) {
				data |= c->bios.ramcfg_10_09_f0 << 20;
				data |= c->bios.ramcfg_10_09_0f << 0;
				mask |= 0x00f0000f;
			}
			memx_mask(memx, 0x10f668, mask, data);
		}

		data = 0x00002000 * !c->bios.rammap_10_04_08;
		mask = 0x00003000;
		memx_mask(memx, 0x10f910, mask, data);
		memx_mask(memx, 0x10f914, mask, data);
	}

	if (!rammap_10_04_01)
		memx_mask(memx, 0x10f824, 0x00006000, 0x00006000);
	else
		gf100_ram_calc_sddr3_r10f658(ram);

	if (!v->rammap_10_04_08 || !c->bios.rammap_10_04_08) {
		data = 0x22222222 * lowspeed;
		mask = 0x22222222;
		for_each_set_bit(fbpa, &ram->base.fbpam, ram->base.fbpan) {
			memx_mask(memx, 0x110620 + (fbpa * 0x1000), mask, data);
			memx_mask(memx, 0x110628 + (fbpa * 0x1000), mask, data);
			memx_mask(memx, 0x110630 + (fbpa * 0x1000), mask, data);
		}
	}

	if (ram->mode != DIV) {
		gf100_ram_calc_sddr3_r10f830(ram, c->freq < somefreq);
		gf100_ram_calc_sddr3_r137370(ram, true);
		memx_mask(memx, 0x137360, 0x00000001, 0x00000000);
	}

	memx_wr32(memx, 0x10f090, 0x4000007f);
	memx_wr32(memx, 0x10f210, 0x80000000);
	memx_nsec(memx, locknsec);

	if (memx_mask(memx, 0x10f304, mr[1].mask, mr[1].data, DIFF))
		memx_nsec(memx, 1000);

	gf100_ram_calc_sddr3_dll_reset(memx);

	memx_mask(memx, 0x10f300, 0x00000000, 0x00000000, FORCE);
	memx_nsec(memx, 1000);

	if (v->ramcfg_10_02_08) {
		data = 0x00001000 * !c->bios.ramcfg_10_02_08;
		mask = 0x00001000;
		memx_mask(memx, 0x10f200, mask, data);
	}

	if (mask = 0, data = 0, v->ramcfg_10_02_20) {
		data |= 0xf0000000 * !!c->bios.ramcfg_10_02_20;
		mask |= 0xf0000000;
	}
	if (v->ramcfg_10_02_04) {
		data |= 0x01000000 * !c->bios.ramcfg_10_02_04;
		mask |= 0x01000000;
	}
	if (v->ramcfg_10_04_03) {
		data |= c->bios.ramcfg_10_04_03 << 8;
		mask |= 0x00000300;
	}
	memx_mask(memx, 0x10f604, mask, data);

	if (mask = 0, data = 0, v->ramcfg_10_02_01) {
		data |= 0x00000100 * !!c->bios.ramcfg_10_02_01;
		mask |= 0x00000100;
	}
	memx_mask(memx, 0x10f614, mask, data);

	if (mask = 0, data = 0, v->ramcfg_10_02_02) {
		data |= 0x00000100 * !!c->bios.ramcfg_10_02_02;
		mask |= 0x00000100;
	}
	memx_mask(memx, 0x10f610, mask, data);

	mask = 0x14000020;
	data = 0x00000000;
	if (v->ramcfg_10_02_10) {
		data |= 0x04000000 * !c->bios.ramcfg_10_02_10;
		data |= 0x08000004 *  c->bios.ramcfg_10_02_10;
		mask |= 0x08000004;
	}
	if (v->ramcfg_10_02_08) {
		data |= 0x00100000 * !c->bios.ramcfg_10_02_08;
		mask |= 0x00100000;
	}
	if (v->ramcfg_10_02_04) {
		data |= 0x12800000 * !c->bios.ramcfg_10_02_04;
		mask |= 0x12800000;
	}
	memx_mask(memx, 0x10f808, mask, data);

	if (v->ramcfg_10_02_10) {
		data = 0x00020000 * !c->bios.ramcfg_10_02_10;
		mask = 0x00030000;
		memx_mask(memx, 0x1373ec, mask, data);
	}

	if (ram->base.func->calc_r1373f8)
		ram->base.func->calc_r1373f8(&ram->base);

	if (v->ramcfg_10_03_0f) {
		data = 0x11111111 * c->bios.ramcfg_10_03_0f;
		mask = 0xffffffff;
		memx_mask(memx, 0x10f618, mask, data);
	}

	ram->base.func->calc_r61c140_100c00(&ram->base);

	if (!c->bios.ramcfg_DLLoff) {
		gf100_ram_calc_sddr3_dll_reset(memx);
		memx_nsec(memx, locknsec);
	} else {
		memx_nsec(memx, 5000);
	}

	memx_mask(memx, 0x10f830, 0x01000000, 0x01000000);
	memx_mask(memx, 0x10f830, 0x01000000, 0x00000000);

	/* Re-enable FB access. */
	gf100_ram_calc_fb_access(ram, true, r100b0c);

	if (r10f200_11)
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
gf100_ram_calc_pll(struct gf100_ram *ram, bool refclk_alt)
{
	struct nvkm_subdev *subdev = &ram->base.fb->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_ram_data *c = ram->base.next;
	struct nvbios_pll pll;
	int khz, N, M, P;
	int mode;

	if (ram->base.type == NVKM_RAM_TYPE_GDDR5)
		mode = PLL2;
	else
		mode = PLL;

	/* Transition clock to use while MPLL is being modified. */
	khz = gf100_ram_calc_src(device, 0x137300, 0x137310,
				 405000 /*XXX: hardcoded, or vbios? */,
				 &ram->dsrc, &ram->dctl);
	if (khz < 0)
		return khz;

	/* MPLL reference clock. */
	if (mode == PLL2) {
		/* MPLLSRC reference clock. */
		khz = gf100_ram_calc_src(device, 0x137320, 0x137330,
					 ram->refpll.refclk,
					 &ram->rsrc, &ram->rctl);
		if (khz < 0)
			return khz;

		/* MPLLSRC. */
		pll = ram->refpll;
		pll.refclk = khz;
		khz = gt215_pll_calc(subdev, &pll,
				     refclk_alt ? ram->mempll.refclk_alt :
						  ram->mempll.refclk,
				     &N, NULL, &M, &P);
		if (khz < 0)
			return khz;

		ram->rpll = (P << 16) | (N << 8) | M;
	} else {
		khz = gf100_ram_calc_src(device, 0x137320, 0x137330,
					 ram->mempll.refclk,
					 &ram->rsrc, &ram->rctl);
		if (khz < 0)
			return khz;
	}

	/* And, finally, MPLL. */
	pll = ram->mempll;
	pll.refclk = khz;
	khz = gt215_pll_calc(subdev, &pll, c->freq, &N, NULL, &M, &P);
	if (khz < 0)
		return khz;

	ram->mpll = (P << 16) | (N << 8) | M;
	ram->mode = mode;
	return khz;
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
	if (!(flags & NVKM_CLK_NO_PLL) && ram->mode == INVALID) {
		int khz = gf100_ram_calc_pll(ram, false);
		if (khz != ram->base.next->freq) {
			int alt = gf100_ram_calc_pll(ram, true);
			if (abs(alt - ram->base.next->freq) >
			    abs(khz - ram->base.next->freq))
				gf100_ram_calc_pll(ram, false);
		}
	}
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
	struct nvbios_ramcfg *v = &ram->base.diff;
	struct nvkm_ram_data c;
	u32 mask, data;
	int ret;

	ret = nvkm_ram_data(&ram->base, khz, &c);
	if (ret)
		return;

	if (mask = 0, data = 0, v->rammap_10_05_0003fe00) {
		data |= c.bios.rammap_10_05_0003fe00 << 12;
		mask |= 0x001ff000;
	}
	if (v->rammap_10_05_000001ff) {
		data |= c.bios.rammap_10_05_000001ff;
		mask |= 0x000001ff;
	}
	nvkm_mask(device, 0x10f468, mask, data);

	if (mask = 0, data = 0, v->rammap_10_05_00040000) {
		data |= c.bios.rammap_10_05_00040000;
		mask |= 0x00000001;
	}
	nvkm_mask(device, 0x10f420, mask, data);

	if (mask = 0, data = 0, v->rammap_10_05_00080000) {
		data |= c.bios.rammap_10_05_00080000;
		mask |= 0x00000001;
	}
	nvkm_mask(device, 0x10f430, mask, data);

	if (mask = 0, data = 0, v->rammap_10_05_01f00000) {
		data |= c.bios.rammap_10_05_01f00000;
		mask |= 0x0000001f;
	}
	nvkm_mask(device, 0x10f400, mask, data);

	if (mask = 0, data = 0, v->rammap_10_05_02000000) {
		data |= c.bios.rammap_10_05_02000000 << 9;
		mask |= 0x00000200;
	}
	nvkm_mask(device, 0x10f410, mask, data);

	if (mask = 0, data = 0, v->rammap_10_05_08000000) {
		data |= c.bios.rammap_10_05_08000000 << 7;
		mask |= 0x00000080;
	}
	if (v->rammap_10_05_04000000) {
		data |= c.bios.rammap_10_05_04000000 << 5;
		mask |= 0x00000020;
	}
	if (v->rammap_10_0a) {
		data |= c.bios.rammap_10_0a << 8;
		mask |= 0x0000ff00;
	}
	nvkm_mask(device, 0x10f444, mask, data);
}

int
gf100_ram_prog(struct nvkm_ram *base)
{
	struct gf100_ram *ram = gf100_ram(base);
	struct nvkm_device *device = ram->base.fb->subdev.device;
	struct nvbios_ramcfg *v = &ram->base.diff;
	struct nvkm_ram_data *c = ram->base.next;

	if (!nvkm_boolopt(device->cfgopt, "NvMemExec", true))
		return 0;

	gf100_ram_prog_10f4xx(ram, 1000);

	nvkm_memx_fini(&ram->memx, true);

	if (ram->base.type != NVKM_RAM_TYPE_GDDR5) {
		if (v->rammap_10_04_01 && !c->bios.rammap_10_04_01) {
			nvkm_mask(device, 0x132018, 0x00004000, 0x00000000);
			nvkm_mask(device, 0x10f824, 0x00002000, 0x00002000);
		}
	}

	if (v->ramcfg_10_02_10) {
		if (c->bios.ramcfg_10_02_10) {
			nvkm_mask(device, 0x10f824, 0x00000180, 0x00000180);
			nvkm_mask(device, 0x10f808, 0x40000000, 0x00000000);
		} else {
			nvkm_mask(device, 0x10f824, 0x00000600, 0x00000600);
		}
	}

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
	struct nvbios_ramcfg *c, *p;
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

	v->rammap_10_04_08 |= c->rammap_10_04_08 != 0;
	v->rammap_10_04_02 |= c->rammap_10_04_02 != 0;
	v->rammap_10_04_01 |= c->rammap_10_04_01 != 0;
	v->rammap_10_05_08000000 |= c->rammap_10_05_08000000 != 0;
	v->rammap_10_05_04000000 |= c->rammap_10_05_04000000 != 0;
	v->rammap_10_05_02000000 |= c->rammap_10_05_02000000 != 0;
	v->rammap_10_05_01f00000 |= c->rammap_10_05_01f00000 != 0;
	v->rammap_10_05_00080000 |= c->rammap_10_05_00080000 != 0;
	v->rammap_10_05_00040000 |= c->rammap_10_05_00040000 != 0;
	v->rammap_10_05_0003fe00 |= c->rammap_10_05_0003fe00 != 0;
	v->rammap_10_05_000001ff |= c->rammap_10_05_000001ff != 0;
	v->rammap_10_0a |= c->rammap_10_0a != 0;
	v->ramcfg_DLLoff |= c->ramcfg_DLLoff != 0;
	v->ramcfg_10_02_20 |= c->ramcfg_10_02_20 != 0;
	v->ramcfg_10_02_10 |= c->ramcfg_10_02_10 != 0;
	v->ramcfg_10_02_08 |= c->ramcfg_10_02_08 != 0;
	v->ramcfg_10_02_04 |= c->ramcfg_10_02_04 != 0;
	v->ramcfg_10_02_02 |= c->ramcfg_10_02_02 != 0;
	v->ramcfg_10_02_01 |= c->ramcfg_10_02_01 != 0;
	v->ramcfg_10_03_40 |= c->ramcfg_10_03_40 != 0;
	v->ramcfg_10_03_20 |= c->ramcfg_10_03_20 != 0;
	v->ramcfg_10_03_0f |= c->ramcfg_10_03_0f != 0;
	v->ramcfg_10_04_20 |= c->ramcfg_10_04_20 != 0;
	v->ramcfg_10_04_10 |= c->ramcfg_10_04_10 != 0;
	v->ramcfg_10_04_08 |= c->ramcfg_10_04_08 != p->ramcfg_10_04_08;
	v->ramcfg_10_04_03 |= c->ramcfg_10_04_03 != 0;
	v->ramcfg_10_09_f0 |= c->ramcfg_10_09_f0 != 0;
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
	u8 strap, ver, hdr;
	int ret, i;

	if (!(ram = kzalloc(sizeof(*ram), GFP_KERNEL)))
		return -ENOMEM;
	*pram = &ram->base;

	ret = gf100_ram_ctor(func, fb, &ram->base);
	if (ret)
		return ret;

	strap = (nvkm_rd32(subdev->device, 0x101000) & 0x0000003c) >> 2;
	if (!nvbios_M0203Em(bios, strap, &ver, &hdr, &ram->M0203))
		return -ENOSYS;

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
	.calc_r61c140_100c00 = gf100_ram_calc_r61c140,
	.prog = gf100_ram_prog,
	.tidy = gf100_ram_tidy,
};

int
gf100_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	return gf100_ram_new_(&gf100_ram, fb, pram);
}

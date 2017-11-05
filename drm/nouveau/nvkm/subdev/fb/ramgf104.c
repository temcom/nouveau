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

#include <subdev/pmu.h>

void
gf104_ram_calc_r100c00(struct nvkm_ram *base)
{
	struct gf100_ram *ram = gf100_ram(base);
	struct nvbios_ramcfg *v = &ram->base.diff;
	struct nvkm_ram_data *c = ram->base.next;
	struct nvkm_memx *memx = ram->memx;
	u32 mask = 0, data = 0;

	if (v->ramcfg_10_02_20) {
		data = 0x04000000 * !!c->bios.ramcfg_10_02_20;
		mask = 0x04000000;
	}
	memx_mask(memx, 0x100c00, mask, data);
}

static const struct nvkm_ram_func
gf104_ram = {
	.upper = 0x0200000000,
	.probe_fbps = gf100_ram_probe_fbps,
	.probe_fbp_ltcs = gf100_ram_probe_fbp_ltcs,
	.probe_fbpas = gf100_ram_probe_fbps,
	.probe_fbpa_amount = gf100_ram_probe_fbpa_amount,
	.init = gf100_ram_init,
	.calc = gf100_ram_calc,
	.calc_r61c140_100c00 = gf104_ram_calc_r100c00,
	.prog = gf100_ram_prog,
	.tidy = gf100_ram_tidy,
};

int
gf104_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	return gf100_ram_new_(&gf104_ram, fb, pram);
}

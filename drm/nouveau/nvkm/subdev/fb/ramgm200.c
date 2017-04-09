/*
 * Copyright 2017 Red Hat Inc.
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

int
gm200_ram_probe_fbp_ltcs(struct nvkm_device *device, int fbp)
{
	u32 ltco = nvkm_rd32(device, 0x021d70 + (fbp * 4));
	u32 ltcs = nvkm_rd32(device, 0x022450);
	u32 ltcm = ((1 << ltcs) - 1) & ~ltco;
	return hweight32(ltcm);
}

int
gm200_ram_probe_fbps(struct nvkm_device *device, unsigned long *opt)
{
	*opt = nvkm_rd32(device, 0x021d38);
	return nvkm_rd32(device, 0x022438);
}

static const struct nvkm_ram_func
gm200_ram = {
	.upper = 0x1000000000,
	.probe_fbps = gm200_ram_probe_fbps,
	.probe_fbp_ltcs = gm200_ram_probe_fbp_ltcs,
	.probe_fbpas = gm107_ram_probe_fbpas,
	.probe_fbpa_amount = gf100_ram_probe_fbpa_amount,
	.init = gk104_ram_init,
	.calc = gk104_ram_calc,
	.prog = gk104_ram_prog,
	.tidy = gk104_ram_tidy,
};

int
gm200_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	return gk104_ram_new_(&gm200_ram, fb, pram);
}

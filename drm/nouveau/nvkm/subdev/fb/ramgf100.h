#ifndef __GF100_RAM_H__
#define __GF100_RAM_H__
#define gf100_ram(p) container_of((p), struct gf100_ram, base)
#include "ram.h"

#include <subdev/bios.h>
#include <subdev/bios/pll.h>

struct gf100_ram {
	struct nvkm_ram base;
	struct nvkm_memx *memx;
	struct nvbios_pll refpll;
	struct nvbios_pll mempll;

	enum {
		DIV,	/* mdiv */
		PLL,	/* mpll */
		PLL2,	/* mpllsrc + mpll */
		INVALID
	} mode, from;
	u32 dsrc;
	u32 dctl;
};

int gf100_ram_new_(const struct nvkm_ram_func *, struct nvkm_fb *,
		   struct nvkm_ram **);
int gf100_ram_calc(struct nvkm_ram *, u8, u32);
int gf100_ram_prog(struct nvkm_ram *);
void gf100_ram_tidy(struct nvkm_ram *);
#endif

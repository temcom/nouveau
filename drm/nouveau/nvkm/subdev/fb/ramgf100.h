#ifndef __GF100_RAM_H__
#define __GF100_RAM_H__
#define gf100_ram(p) container_of((p), struct gf100_ram, base)
#include "ram.h"

#include <subdev/bios.h>
#include <subdev/bios/M0203.h>
#include <subdev/bios/pll.h>

struct gf100_ram {
	struct nvkm_ram base;
	struct nvkm_memx *memx;

	struct nvbios_M0203E M0203;
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
	u32 rsrc;
	u32 rctl;
	u32 rpll;
	u32 mpll;
};

int gf100_ram_new_(const struct nvkm_ram_func *, struct nvkm_fb *,
		   struct nvkm_ram **);
int gf100_ram_calc(struct nvkm_ram *, u8, u32);
void gf100_ram_calc_r61c140(struct nvkm_ram *);
int gf100_ram_prog(struct nvkm_ram *);
void gf100_ram_tidy(struct nvkm_ram *);

void gf104_ram_calc_r100c00(struct nvkm_ram *);
void gf104_ram_calc_r1373f8(struct nvkm_ram *);
#endif

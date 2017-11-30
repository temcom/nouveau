/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_FB_RAM_PRIV_H__
#define __NVKM_FB_RAM_PRIV_H__
#include "priv.h"
#include <subdev/bios/M0209.h>

int  nvkm_ram_ctor(const struct nvkm_ram_func *, struct nvkm_fb *,
		   enum nvkm_ram_type, u64 size, struct nvkm_ram *);
int  nvkm_ram_new_(const struct nvkm_ram_func *, struct nvkm_fb *,
		   enum nvkm_ram_type, u64 size, struct nvkm_ram **);
void nvkm_ram_del(struct nvkm_ram **);
int  nvkm_ram_init(struct nvkm_ram *);
int nvkm_ram_data(struct nvkm_ram *, u32 khz, struct nvkm_ram_data *);

extern const struct nvkm_ram_func nv04_ram_func;

int  nv50_ram_ctor(const struct nvkm_ram_func *, struct nvkm_fb *,
		   struct nvkm_ram *);

int  gf100_ram_ctor(const struct nvkm_ram_func *, struct nvkm_fb *,
		    struct nvkm_ram *);
int gf100_ram_probe_fbps(struct nvkm_device *, unsigned long *);
int gf100_ram_probe_fbp_ltcs(struct nvkm_device *, int);
u32  gf100_ram_probe_fbpa_amount(struct nvkm_device *, int);
int gf100_ram_init(struct nvkm_ram *);

int gf108_ram_probe_fbpas(struct nvkm_device *, unsigned long *);

int gk104_ram_new_(const struct nvkm_ram_func *, struct nvkm_fb *,
		   struct nvkm_ram **);
void *gk104_ram_dtor(struct nvkm_ram *);
int gk104_ram_init(struct nvkm_ram *);
int gk104_ram_calc(struct nvkm_ram *, u8, u32);
int gk104_ram_prog(struct nvkm_ram *);
void gk104_ram_tidy(struct nvkm_ram *);

int gm107_ram_probe_fbpas(struct nvkm_device *, unsigned long *);

int gm200_ram_probe_fbps(struct nvkm_device *, unsigned long *);
int gm200_ram_probe_fbp_ltcs(struct nvkm_device *, int);

/* DDR link training */
struct gt215_ram_train {
	u16 mask;
	struct nvbios_M0209S remap;
	struct nvbios_M0209S type00;
	struct nvbios_M0209S type01;
	struct nvbios_M0209S type04;
	struct nvbios_M0209S type05;
	struct nvbios_M0209S type06;
	struct nvbios_M0209S type07;
	struct nvbios_M0209S type08;
	struct nvbios_M0209S type09;
};
int gt215_ram_train_type(struct nvkm_ram *ram, int i, u8 ramcfg,
		     struct gt215_ram_train *train);
int gf100_ram_train_init(struct nvkm_ram *ram);

/* RAM type-specific MR calculation routines */
struct nvkm_ram_mr_xlat {
	u8 ival;
	u8 oval;
};

#define MR_ARGS(VARS...)                                                       \
	struct nvbios_ramcfg *v = &ram->diff, *c = &ram->next->bios; (void)v;  \
	union {                                                                \
		struct {                                                       \
			int VARS;                                              \
		} v;                                                           \
		int data[0];                                                   \
	} a;                                                                   \
	int i;                                                                 \
	for (i = 0; i < sizeof(a) / sizeof(a.data[0]); i++)                    \
		a.data[i] = -1;                                                \
	memset(ram->mr, 0x00, sizeof(ram->mr))
#define MR_LOAD(n,d) a.v.n = (d)
#define MR_COND(n,d,c) do { if (c) a.v.n = (d); } while(0)
#define MR_XLAT(n,x) ({                                                        \
	const struct nvkm_ram_mr_xlat *_x = (x);                               \
	int _ret = -EINVAL, _i;                                                \
	for (_i = 0; _i < ARRAY_SIZE(x); _i++) {                               \
		if (_x[_i].ival == a.v.n) {                                    \
			a.v.n = _x[_i].oval;                                   \
			_ret = 0;                                              \
			break;                                                 \
		}                                                              \
	}                                                                      \
	_ret;                                                                  \
})
#define MR_DATA(r,m,n,d) do {                                                  \
	if (a.v.n >= 0) {                                                      \
		ram->mr[(r)].mask |= (m);                                      \
		ram->mr[(r)].data |= (d) << __ffs(m);                          \
	}                                                                      \
} while(0)
#define MR_MASK(r,m,n)   MR_DATA(r, m, n, a.v.n)
#define MR_BITS(r,m,n,b) MR_DATA(r, m, n, (a.v.n & (b)) >> __ffs(b))

int nvkm_sddr2_calc(struct nvkm_ram *);
int nvkm_sddr3_calc(struct nvkm_ram *);
int nvkm_gddr3_calc(struct nvkm_ram *);
int nvkm_gddr5_calc(struct nvkm_ram *, bool nuts, int rq, int l3, int at);

int nv04_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv10_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv1a_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv20_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv40_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv41_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv44_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv49_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv4e_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv50_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gt215_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int mcp77_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gf100_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gf104_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gf108_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gk104_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gm107_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gm200_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gp100_ram_new(struct nvkm_fb *, struct nvkm_ram **);
#endif

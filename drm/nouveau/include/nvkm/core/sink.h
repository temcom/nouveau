#ifndef __NVKM_SINK_H__
#define __NVKM_SINK_H__
#include <nvif/os.h>

struct nvkm_sink {
	const struct nvkm_sink_func *func;
	struct nvkm_sink *parent;
	struct list_head items;
};

struct nvkm_sink_func {
	u32  (*rd32)(struct nvkm_sink *, u32 addr);
	void (*wr32)(struct nvkm_sink *, u32 addr, u32 data);
	void (*wait)(struct nvkm_sink *, u32 addr, u32 mask, u32 data, u64 nsec);
	void (*nsec)(struct nvkm_sink *, u64 nsec);
};

void nvkm_sink_ctor(const struct nvkm_sink_func *, struct nvkm_sink *parent,
		    struct nvkm_sink *);
void nvkm_sink_dtor(struct nvkm_sink *);
struct nvkm_sink_item *nvkm_sink_name(struct nvkm_sink *, u32 name, u32 addr);
struct nvkm_sink_item *nvkm_sink_have(struct nvkm_sink *, u32 name);

u32 nvkm_sink_rd32(struct nvkm_sink *, u32 name);
void nvkm_sink_wr32(struct nvkm_sink *, u32 name, u32 data);
void nvkm_sink_wait(struct nvkm_sink *, u32 name, u32 mask, u32 data, u64 nsec);
void nvkm_sink_nsec(struct nvkm_sink *, u64 nsec);

#define nvri_rd32(s,a)       nvkm_sink_rd32((s), (a))
#define nvri_wr32(s,a,d)     nvkm_sink_wr32((s), (a), (d))
#define nvri_wait(s,a,m,d,n) nvkm_sink_wait((s), (a), (m), (d), (n))
#define nvri_nsec(s,n)       nvkm_sink_nsec((s), (n))
#define nvri_exec(s,e)       nvkm_sink_exec((s), (e))

#define NVKM_SINK_MASK_FORCE 0x01
#define NVKM_SINK_MASK_DIFF  0x02

static inline u32
nvkm_sink_mask(struct nvkm_sink *sink, u32 name, u32 mask, u32 data, u8 flags)
{
	u32 prev = nvkm_sink_rd32(sink, name);
	u32 next = (prev & ~mask) | data;
	if (next != prev || (flags & NVKM_SINK_MASK_FORCE))
		nvkm_sink_wr32(sink, name, next);
	WARN_ON(data & ~mask);
	return (flags & NVKM_SINK_MASK_DIFF) ? prev ^ next : prev;
}

#define nvri_mask_6(s,a,m,d,f1,f2) nvkm_sink_mask((s), (a), (m), (d),          \
						  NVKM_SINK_MASK_##f1 |        \
						  NVKM_SINK_MASK_##f2)
#define nvri_mask_5(s,a,m,d,f1)    nvkm_sink_mask((s), (a), (m), (d),          \
						  NVKM_SINK_MASK_##f1)
#define nvri_mask_4(s,a,m,d)       nvkm_sink_mask((s), (a), (m), (d), 0)
#define nvri_mask_(x,s,a,m,d,f1,f2,c,v...) c
#define nvri_mask(v...)                                                        \
	nvri_mask_(x, ##v, nvri_mask_6, nvri_mask_5, nvri_mask_4)(v)
#endif

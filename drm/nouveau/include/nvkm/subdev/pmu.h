/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_PMU_H__
#define __NVKM_PMU_H__
#include <core/subdev.h>
#include <engine/falcon.h>

struct nvkm_pmu {
	const struct nvkm_pmu_func *func;
	struct nvkm_subdev subdev;
	struct nvkm_falcon *falcon;
	struct nvkm_msgqueue *queue;

	struct {
		u32 base;
		u32 size;
	} send;

	struct {
		u32 base;
		u32 size;

		struct work_struct work;
		wait_queue_head_t wait;
		u32 process;
		u32 message;
		u32 data[2];
	} recv;
};

int nvkm_pmu_send(struct nvkm_pmu *, u32 reply[2], u32 process,
		  u32 message, u32 data0, u32 data1);
void nvkm_pmu_pgob(struct nvkm_pmu *, bool enable);

int gt215_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);
int gf100_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);
int gf119_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);
int gk104_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);
int gk110_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);
int gk208_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);
int gk20a_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);
int gm107_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);
int gm20b_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);
int gp100_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);
int gp102_pmu_new(struct nvkm_device *, int, struct nvkm_pmu **);

/* interface to MEMX process running on PMU */
struct nvkm_memx {
	struct nvkm_sink sink;
};

#define memx_rd32(s,a)          nvri_rd32(&(s)->sink, (a))
#define memx_wr32(s,a,d)        nvri_wr32(&(s)->sink, (a), (d))
#define memx_mask(s,a,m,d,v...) nvri_mask(&(s)->sink, (a), (m), (d), ##v)
#define memx_nsec(s,n)		nvri_nsec(&(s)->sink, (n))
#define memx_wait(s,a,m,d,n)	nvri_wait(&(s)->sink, (a), (m), (d), (n))

int nvkm_memx_init(struct nvkm_pmu *, struct nvkm_memx **);
int nvkm_memx_fini(struct nvkm_memx **, bool exec);
void nvkm_memx_wait_vblank(struct nvkm_memx *);
void nvkm_memx_block(struct nvkm_memx *);
void nvkm_memx_unblock(struct nvkm_memx *);
void nvkm_memx_train(struct nvkm_memx *);
int  nvkm_memx_train_result(struct nvkm_pmu *, u32 *, int);
void nvkm_memx_fbpa_war_nsec(struct nvkm_memx *, u32 nsec);
#endif

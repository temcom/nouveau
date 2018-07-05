#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <sys/mman.h>

#include <nvif/class.h>

#include <xf86drm.h>
#include <nouveau/nouveau.h>
#include <uapi/drm/nouveau_drm.h>

#include "util.h"

static int fd;

static u64
reserve_vma(u64 reserved_size, bool low)
{
	void *reserved = mmap(NULL, reserved_size, PROT_NONE, MAP_PRIVATE |
			      (low ? MAP_32BIT : 0) | MAP_ANONYMOUS, -1, 0);
	if (reserved == MAP_FAILED)
		return 0;
	return (unsigned long)reserved;
}

static u64 reserved_addr;
static u64 reserved_size;
static struct nouveau_drm *drm;
static struct nouveau_device *device;
static u8 __attribute__((aligned(4096))) temp[16 << 20];

static struct nouveau_object *chan;
static struct nouveau_client *client;
static struct nouveau_pushbuf *push;
static struct nouveau_object *compute;

static void
DATA(u32 data) {
	*push->cur++ = data;
}

static void
INCR(int s, u32 m, int n)
{
	DATA(0x20000000 | ((n) << 16) | ((s) << 13) | ((m) >> 2));
}

static void
NINC(int s, u32 m, int n)
{
	DATA(0x60000000 | ((n) << 16) | ((s) << 13) | ((m) >> 2));
}

static void
IMMD(int s, u32 m, u16 d)
{
	DATA(0x80000000 | ((d) << 16) | ((s) << 13) | ((m) >> 2));
}

static void
KICK(void)
{
	nouveau_pushbuf_kick(push, push->channel);
}

struct gp100_cp_launch_desc
{
   u32 unk0[8];
   u32 entry;
   u32 unk9[2];
   u32 unk11_0      : 30;
   u32 linked_tsc   : 1;
   u32 unk11_31     : 1;
   u32 griddim_x    : 31;
   u32 unk12        : 1;
   u16 griddim_y;
   u16 unk13;
   u16 griddim_z;
   u16 unk14;
   u32 unk15[2];
   u32 shared_size  : 18;
   u32 unk17        : 14;
   u16 unk18;
   u16 blockdim_x;
   u16 blockdim_y;
   u16 blockdim_z;
   u32 cb_mask      : 8;
   u32 unk20        : 24;
   u32 unk21[8];
   u32 local_size_p : 24;
   u32 unk29        : 3;
   u32 bar_alloc    : 5;
   u32 local_size_n : 24;
   u32 gpr_alloc    : 8;
   u32 cstack_size  : 24;
   u32 unk31        : 8;
   struct {
      u32 address_l;
      u32 address_h : 17;
      u32 reserved  : 2;
      u32 size_sh4  : 13;
   } cb[8];
   u32 unk48[16];
};

struct gv100_cp_launch_desc {
   u32 unkn_00;
   u32 unkn_04;
   u32 unkn_08;
   u32 unkn_0c;
   u32 unkn_10;
   u32 unkn_14;
   u32 unkn_18;
   u32 unkn_1c;
   u32 unkn_20;
   u32 unkn_24;
   u32 unkn_28;
   u32 unkn_2c;
   u32 griddim_x;
   u32 griddim_y;
   u32 griddim_z;
   u32 unkn_3c;
   u32 unkn_40;
   u32 unkn_44;
   u16 unkn_48;
   u16 blockdim_x;
   u16 blockdim_y;
   u16 blockdim_z;
   u16 cb_mask:8;
   u16 gpr_alloc:8;
   u16 unkn_52;
   u32 unkn_54;
   u32 unkn_58;
   u32 unkn_5c;
   u32 unkn_60;
   u32 unkn_64;
   u32 unkn_68;
   u32 unkn_6c;
   u32 unkn_70;
   u32 unkn_74;
   u32 unkn_78;
   u32 unkn_7c;
   struct {
      u32 address_l;
      u32 address_h:17;
      u32 reserved:2;
      u32 size_sh4  : 13;
   } cb[8];
   uint64_t entry;
   u32 unkn_c8;
   u32 unkn_cc;
   u32 unkn_d0;
   u32 unkn_d4;
   u32 unkn_d8;
   u32 unkn_dc;
   u32 unkn_e0;
   u32 unkn_e4;
   u32 unkn_e8;
   u32 unkn_ec;
   u32 unkn_f0;
   u32 unkn_f4;
   u32 unkn_f8;
   u32 unkn_fc;
} __attribute__ ((packed));

static struct nouveau_bo *desc;

static u32 __attribute__((aligned(256)))
kernel_ldc_stg_sm60[] = {
	0xfc0007e0,
	0x001f8000,
	0x00170000,
	0x4c980780,
	0x00070001,
	0x4c980780,
	0x00270002,
	0x4c980780,
	0xfc0007e0,
	0x001f8000,
	0x00370003,
	0x4c980780,
	0x02170004,
	0xf0c80000,
	0x00470300,
	0x5a000000,
	0xfc0007e0,
	0x001f8000,
	0x00070002,
	0xbf900000,
	0x0007000f,
	0xe3000000,
	0x0007000f,
	0xe3000000,
};

static u32 __attribute__((aligned(256)))
kernel_ldc_ldg_stg_sm60[] = {
	0xfc0007e0,
	0x001f8000,
	0x00170000,
	0x4c980780,
	0x00070001,
	0x4c980780,
	0x00270002,
	0x4c980780,
	0xfc0007e0,
	0x001f8000,
	0x00370003,
	0x4c980780,
	0x02170004,
	0xf0c80000,
	0x00470300,
	0x5a000000,
	0xfc0007e0,
	0x001f8000,
	0x0ff70407,
	0x5b640380,
	0x00000002,
	0xbf900000,
	0x00080003,
	0x9c900000,
	0xfc0007e0,
	0x001f8000,
	0x00070002,
	0xbf900000,
	0x0007000f,
	0xe3000000,
	0x0007000f,
	0xe3000000,
};

static u32 __attribute__((aligned(256)))
kernel_ldc_stg_sm70[] = {
	0x00007a02,
	0x00000100,
	0x00000f00,
	0x000fc000,
	0x00017a02,
	0x00000000,
	0x00000f00,
	0x000fc000,
	0x00027a02,
	0x00000200,
	0x00000f00,
	0x000fc000,
	0x00037a02,
	0x00000300,
	0x00000f00,
	0x000fc000,
	0x00047919,
	0x00000000,
	0x00002100,
	0x000fc000,
	0x03007224,
	0x00000004,
	0x00000000,
	0x000fc000,
	0x00007385,
	0x00000000,
	0x00114902,
	0x000fc000,
	0x0000794d,
	0x00000000,
	0x03800000,
	0x000fc000,
};

static u32 __attribute__((aligned(256)))
kernel_ldc_ldg_stg_sm70[] = {
	0x00007a02,
	0x00000100,
	0x00000f00,
	0x000fc000,
	0x00017a02,
	0x00000000,
	0x00000f00,
	0x000fc000,
	0x00027a02,
	0x00000200,
	0x00000f00,
	0x000fc000,
	0x00037a02,
	0x00000300,
	0x00000f00,
	0x000fc000,
	0x00047919,
	0x00000000,
	0x00002100,
	0x000fc000,
	0x03007224,
	0x00000004,
	0x00000000,
	0x000fc000,
	0x0400780c,
	0x00000000,
	0x03f02070,
	0x000fc000,
	0x00000385,
	0x00000000,
	0x00114902,
	0x000fc000,
	0x00038980,
	0x00000000,
	0x00114902,
	0x000fc000,
	0x00007385,
	0x00000000,
	0x00114902,
	0x000fc000,
	0x0000794d,
	0x00000000,
	0x03800000,
	0x000fc000,
};

enum {
	LDC_STG = 0,
	LDC_LDG_STG,
};

static u32 *
kernel_sm60[] = {
	[LDC_STG    ] = kernel_ldc_stg_sm60,
	[LDC_LDG_STG] = kernel_ldc_ldg_stg_sm60,
};

static u32 *
kernel_sm70[] = {
	[LDC_STG    ] = kernel_ldc_stg_sm70,
	[LDC_LDG_STG] = kernel_ldc_ldg_stg_sm70,
};

static u32 **kernel;

static void
chan_fini(void)
{
	nouveau_object_del(&compute);
	nouveau_pushbuf_del(&push);
	nouveau_object_del(&chan);
}

#include "uapi/drm/nouveau_drm.h"

static int
chan_init(void)
{
	static const struct nouveau_mclass computes[] = {
		{  VOLTA_COMPUTE_A, -1 },
		{ PASCAL_COMPUTE_B, -1 },
		{ PASCAL_COMPUTE_A, -1 },
		{}
	};
	int ret;

	chan_fini();

	/* Allocate channel. */
	ret = nouveau_object_new(&device->object, 0, NOUVEAU_FIFO_CHANNEL_CLASS,
				 &(struct nvc0_fifo) {},
				 sizeof(struct nvc0_fifo), &chan);
	if (ret < 0)
		return 1;

	ret = nouveau_pushbuf_new(client, chan, 1, 128 * 1024, 1, &push);
	if (ret < 0)
		return 1;

	ret = nouveau_pushbuf_space(push, 1024, 0, 1);
	if (ret < 0)
		return 1;

	/* Allocate compute, and test subchannel bind. */
	ret = nouveau_object_mclass(chan, computes);
	if (ret < 0)
		return 1;

	ret = nouveau_object_new(chan, 0xbeefc0c0, computes[ret].oclass,
				 NULL, 0, &compute);
	if (ret < 0)
		return 1;

	if (compute->oclass < VOLTA_COMPUTE_A)
		kernel = kernel_sm60;
	else
		kernel = kernel_sm70;

	INCR(1, 0x0000, 1);
	DATA(compute->oclass);

	/* Setup compute resources. */
	const int mp_count = 16;
	INCR(1, 0x0790, 2);
	DATA(upper_32_bits((unsigned long)temp));
	DATA(lower_32_bits((unsigned long)temp));
	INCR(1, 0x02e4, 3);
	DATA(upper_32_bits(ARRAY_SIZE(temp) / mp_count));
	DATA(lower_32_bits(ARRAY_SIZE(temp) / mp_count) & ~0x7fff);
	DATA(0x000000ff);
	if (compute->oclass < VOLTA_COMPUTE_A) {
		INCR(1, 0x02f0, 3);
		DATA(upper_32_bits(ARRAY_SIZE(temp) / mp_count));
		DATA(lower_32_bits(ARRAY_SIZE(temp) / mp_count) & ~0x7fff);
		DATA(0x000000ff);
		INCR(1, 0x077c, 1);
		DATA(0xff000000);
		INCR(1, 0x0214, 1);
		DATA(0xfe000000);
	}
	INCR(1, 0x0310, 1);
	DATA(0x00000400);
	NINC(1, 0x0248, 64);
	for (int i = 63; i >= 0; --i)
		DATA(0x00038000 | i);
	IMMD(1, 0x0110, 0x0000);
	INCR(1, 0x2608, 1);
	DATA(0);
	KICK();
	return 0;
}

static void
bind(u64 addr, u64 size)
{
	struct drm_nouveau_svm_bind args = {
		.header = NOUVEAU_SVM_BIND_COMMAND__MIGRATE <<
			  NOUVEAU_SVM_BIND_COMMAND_SHIFT |
			  0 << NOUVEAU_SVM_BIND_PRIORITY_SHIFT |
			  NOUVEAU_SVM_BIND_TARGET__GPU_VRAM <<
			  NOUVEAU_SVM_BIND_TARGET_SHIFT,
		.va_start = addr,
		.va_end = addr + size,
		.npages = size >> 12,
		.stride = 0,
	};
	int ret;

	ret = drmCommandWrite(fd, DRM_NOUVEAU_SVM_BIND, &args, sizeof(args));
	printf("bind: %d\n", ret);
}

struct test_parm {
	u32 addrhi;
	u32 addrlo;
	u32 data;
	u32 stride;
};

#define PARM_RW   1
#define PARM_VRAM 2
#define OUTP_VRAM 4

static void
test(void *code, int threads, u32 stride, u32 flags,
     struct test_parm *parm, u32 *outp, bool killed)
{
	u32 value = rand();

	mprotect(parm, 4096, PROT_READ | PROT_WRITE);
	parm->addrhi = upper_32_bits((unsigned long)(void *)outp);
	parm->addrlo = lower_32_bits((unsigned long)(void *)outp);
	parm->data = value;
	parm->stride = stride;
	if (!(flags & PARM_RW))
		mprotect(parm, 4096, PROT_READ);
	if ( (flags & PARM_VRAM))
		bind((unsigned long)(void *)parm, PAGE_SIZE);
	if ( (flags & OUTP_VRAM))
		bind((unsigned long)(void *)outp, PAGE_SIZE);

	printf("code %p parm %p outp %p%s\n", code, parm, outp,
	       killed ? " - should fault, can take up to 30s to recover" : "");

	if (compute->oclass < VOLTA_COMPUTE_A) {
		struct gp100_cp_launch_desc *d = desc->map;
		memset(d, 0x00, sizeof(*d));
		d->unk0[4]  = 0x40;
		d->unk11_0  = 0x04014000;
		d->entry = 0;
		d->griddim_x = 1;
		d->griddim_y = 1;
		d->griddim_z = 1;
		d->blockdim_x = threads;
		d->blockdim_y = 1;
		d->blockdim_z = 1;
		d->shared_size = 0;
		d->local_size_p = 0;
		d->local_size_n = 0;
		d->cstack_size = 0x800;
		d->gpr_alloc = 5;
		d->bar_alloc = 0;
		d->cb[0].address_l = lower_32_bits((unsigned long)(void *)parm);
		d->cb[0].address_h = upper_32_bits((unsigned long)(void *)parm);
		d->cb[0].size_sh4 = DIV_ROUND_UP(sizeof(*parm), 16);
		d->cb_mask = 1;
	} else {
		struct gv100_cp_launch_desc *d = desc->map;
		memset(d, 0x00, sizeof(*d));
		d->entry = (unsigned long)code;
		d->griddim_x = 1;
		d->griddim_y = 1;
		d->griddim_z = 1;
		d->blockdim_x = threads;
		d->blockdim_y = 1;
		d->blockdim_z = 1;
		d->gpr_alloc = 7;
		d->cb[0].address_l = lower_32_bits((unsigned long)(void *)parm);
		d->cb[0].address_h = upper_32_bits((unsigned long)(void *)parm);
		d->cb[0].size_sh4 = DIV_ROUND_UP(sizeof(*parm), 16);
		d->cb_mask = 0x1;
	}

	INCR(1, 0x1698, 1);
	DATA(0x00001000);
	INCR(1, 0x021c, 1);
	DATA(0x00001017);
	if (compute->oclass < VOLTA_COMPUTE_A) {
		INCR(1, 0x1608, 2);
		DATA(upper_32_bits((unsigned long)code));
		DATA(lower_32_bits((unsigned long)code));
		INCR(1, 0x0274, 3);
		DATA(upper_32_bits((unsigned long)(void *)parm));
		DATA(lower_32_bits((unsigned long)(void *)parm));
		DATA(0x80000010);
	}

	INCR(1, 0x02b4, 1);
	DATA(desc->offset >> 8);
	INCR(1, 0x02bc, 1);
	DATA(0x00000003);
	INCR(1, 0x0110, 1);
	DATA(0x00000000);

	nouveau_pushbuf_refn(push, &(struct nouveau_pushbuf_refn) {
				desc, NOUVEAU_BO_VRAM | NOUVEAU_BO_GART |
				NOUVEAU_BO_RD,
			     }, 1);
	KICK();
	nouveau_bo_wait(desc, NOUVEAU_BO_WR, client);

	printf("\t%08x %08x - ", outp[0], value);
	if (!killed) {
		if (outp[0] != value)
			printf("FAIL!!!");
		else
			printf("OK.");
	} else {
		if (outp[0] == value)
			printf("FAIL???");
		else
			printf("OK - KILLED.");
		chan_init();
	}
	printf("\n");
}

int
main(int argc, char **argv)
{
	int ret;

	srand(time(NULL));

	fd = open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0)
		return 1;

	/* Open DRM. */
	ret = nouveau_drm_new(fd, &drm);
	if (ret < 0)
		return 1;

	ret = nouveau_device_new(&drm->client, NV_DEVICE,
				 &(struct nv_device_v0) {
					.device = ~0ULL,
				 }, sizeof(struct nv_device_v0), &device);
	if (ret < 0)
		return 1;

	/* Reserve a chunk of (low) address-space for non-SVM buffers. */
	reserved_size = 512 * 1024 * 1024;
	reserved_addr = reserve_vma(reserved_size, true);
	assert(reserved_addr);
	printf("unmanaged: %016llx %016llx\n", reserved_addr, reserved_size);

	/* Enable SVM. */
	struct drm_nouveau_svm_init svm_args = {
		.unmanaged_addr = reserved_addr,
		.unmanaged_size = reserved_size,
	};

	ret = drmCommandWrite(drm->fd, DRM_NOUVEAU_SVM_INIT,
			      &svm_args, sizeof(svm_args));
	if (ret < 0)
		return 1;

	/* QMD doesn't seem to support replayable faults, normal buffer
	 * allocation required!
	 */
	ret = nouveau_client_new(device, &client);
	if (ret < 0)
		return 1;

	ret = nouveau_bo_new(device, NOUVEAU_BO_VRAM | NOUVEAU_BO_GART |
			     NOUVEAU_BO_MAP, 0, 0x1000, NULL, &desc);
	if (ret < 0)
		return 1;

	ret = nouveau_bo_map(desc, NOUVEAU_BO_WR, client);
	if (ret < 0)
		return 1;

	if (chan_init())
		return 1;

	void *code = kernel[LDC_STG];
	void *parm, *outp;

#if 1
	//XXX: simple test
	parm = aligned_alloc(4096, 4096);
	outp = aligned_alloc(4096, 4096);
	test(code, 1, 0x1000, 0, parm, outp, false);
	free(outp);

	//XXX: upgrading from read fault to write fault
	outp = (u8 *)parm + 0x0200;
	test(code, 1, 0x1000, PARM_RW, parm, outp, false);
	free(parm);

	//XXX: allocation before unmanaged area, shorten fault window
	parm = mmap((void *)((unsigned long)reserved_addr - 0x1000), 0x1000,
		    PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	outp = aligned_alloc(4096, 4096);
	test(code, 1, 0x1000, 0, parm, outp, false);
	munmap(parm, 0x1000);
#endif

#if 1
	//XXX: merging of faults within window (consecutive)
	parm = aligned_alloc(4096, 4096);
	outp = aligned_alloc(4096, 8192);
	test(code, 2, 0x1000, 0, parm, outp, false);
	free(outp);
	free(parm);
#endif

#if 1
	//XXX: merging of faults within window (non-consecutive)
	parm = aligned_alloc(4096, 4096);
	outp = aligned_alloc(4096, 16384);
	test(code, 2, 0x2000, 0, parm, outp, false);
	free(outp);
	free(parm);
#endif

#if 1
	//XXX: read+write faults to same page
	//     - svm should handle as a write fault
	//     -XXX: probably need a better shader, need to run
	//           multiple times to trigger this case!
	parm = aligned_alloc(4096, 4096);
	outp = aligned_alloc(4096, 4096);
	test(kernel[LDC_LDG_STG], 2, 0x0000, 0, parm, outp, false);
	free(outp);
	free(parm);
#endif

	//XXX: gpu write to read-only page?
#if 0 /* volta channel recovery not terribly reliable yet... */
	parm = aligned_alloc(4096, 4096);
	outp = aligned_alloc(4096, 4096);
	mprotect(outp, 0x1000, PROT_READ);
	test(code, 1, 0x1000, 0, parm, outp, true);
	mprotect(outp, 0x1000, PROT_READ | PROT_WRITE);
	test(code, 1, 0x1000, 0, parm, outp, false);
	free(outp);
	free(parm);
#endif

	//XXX: fault in unmanaged area
#if 0 /* volta channel recovery not terribly reliable yet... */
	munmap((void *)(unsigned long)reserved_addr, reserved_size);
	parm = mmap((void *)((unsigned long)reserved_addr + (256*1024*1024)),
		    0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE |
		    MAP_ANONYMOUS, -1, 0);
	outp = aligned_alloc(4096, 4096);
	mprotect(outp, 0x1000, PROT_READ | PROT_WRITE);
	test(code, 1, 0x1000, 0, parm, outp, true);
	munmap(parm, 0x1000);
	parm = aligned_alloc(4096, 4096);
	test(code, 1, 0x1000, 0, parm, outp, false);
	free(outp);
#endif

#if 1
	//XXX: input migrated to vram
	parm = aligned_alloc(4096, 4096);
	outp = aligned_alloc(4096, 4096);
	test(code, 1, 0x1000, PARM_VRAM, parm, outp, false);
	free(outp);
	//XXX: input migrated back to host when touched by cpu again
	outp = aligned_alloc(4096, 4096);
	test(code, 1, 0x1000, 0, parm, outp, false);
	free(outp);
	free(parm);
#endif

#if 1
	//XXX: output migrated to vram for op, back to host when reading result
	parm = aligned_alloc(4096, 4096);
	outp = aligned_alloc(4096, 4096);
	((u32 *)outp)[0] = 0; // dirty page or it won't be migrated..
	test(code, 1, 0x1000, OUTP_VRAM, parm, outp, false);
	free(outp);
	free(parm);
#endif

#if 1
	//XXX: free with input left in vram, hits sleep from invalid ctx..
	parm = aligned_alloc(4096, 4096);
	outp = aligned_alloc(4096, 4096);
	test(code, 1, 0x1000, PARM_VRAM, parm, outp, false);
	free(outp);
	free(parm);
#endif

	/* Cleanup! */
	printf("done!\n");
	getchar();
	return 0;
}

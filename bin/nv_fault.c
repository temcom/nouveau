#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <sys/mman.h>

#include <nvif/client.h>
#include <nvif/driver.h>
#include <nvif/device.h>
#include <nvif/fifo.h>
#include <nvif/mem.h>
#include <nvif/notify.h>
#include <nvif/vmm.h>

#include <nvif/class.h>
#include <nvif/cla06f.h>
#include <nvif/clb069.h>
#include <nvif/ifc00d.h>

#include "util.h"

static struct nvif_device device;
static struct nvif_mmu mmu;
static struct nvif_vmm vmm;
static u64 reserved_addr;
static u64 reserved_size;

static u64
reserve_vma(u64 reserved_size, bool low)
{
	void *reserved = mmap(NULL, reserved_size, PROT_NONE, MAP_PRIVATE |
			      (low ? MAP_32BIT : 0) | MAP_ANONYMOUS, -1, 0);
	if (reserved == MAP_FAILED)
		return 0;
	return (unsigned long)reserved;
}

struct vram {
	struct nvif_mem mem;
	struct nvif_vma vma;
};

static void
vram_put(struct nvif_device *device, struct vram *vram)
{
	munmap((void *)(unsigned long)vram->vma.addr, vram->mem.size);
	nvif_mem_fini(&vram->mem);
}

static void
vram_get(struct nvif_device *device, u64 size, struct vram *vram, bool low, bool vma)
{
	int ret;

	ret = nvif_mem_init(&mmu, NVIF_CLASS_MEM_GF100, NVIF_MEM_VRAM |
			    NVIF_MEM_MAPPABLE, 0, size, NULL, 0,
			    &vram->mem);
	assert(ret == 0);

	if (vma) {
		vram->vma.addr = reserve_vma(vram->mem.size, low);
		assert(vram->vma.addr);

		ret = nvif_vmm_map(&vmm, vram->vma.addr, vram->mem.size,
				   NULL, 0, &vram->mem, 0);
		assert(ret == 0);
	} else {
		vram->vma.addr = 0;
	}

	ret = nvif_object_map(&vram->mem.object, NULL, 0);
	assert(ret == 0);
}

static u64 map_wndw = 16;
static u64 map_addr;
static bool map_back;

static void
memclr(u64 _addr, u64 _size, u8 page, bool eot)
{
	const u64 addr = map_back ?
		  map_addr + ((map_wndw - _addr - _size) << page)
		: map_addr + (_addr << page);
	int ret;

	ret = nvif_vmm_unmap(&vmm, addr);
	assert(ret == 0);
	if (eot)
		printf("\n\n\n");
}

static void
memmap(u64 _addr, u64 _size, u8 page, struct nvif_mem *mem, bool fail)
{
	const u64 addr = map_back ?
		  map_addr + ((map_wndw - _addr - _size) << page)
		: map_addr + (_addr << page);
	const u64 size = _size << page;
	int ret;

	ret = nvif_vmm_map(&vmm, addr, size, NULL, 0, mem, 0);
	assert((!fail && !ret) || (fail && ret));
}

static void
pfnclr(u64 _addr, u64 _size, u8 page, bool eot)
{
	const u64 addr = map_back ?
		  map_addr + ((map_wndw - _addr - _size) << page)
		: map_addr + (_addr << page);
	const u64 size = _size << page;
	int ret;

	printf("pfnclr %016llx %016llx\n", addr, size);

	ret = nvif_object_mthd(&vmm.object, NVIF_VMM_V0_PFNCLR,
			       &(struct nvif_vmm_pfnclr_v0) {
					.addr = addr,
					.size = size,
			       }, sizeof(struct nvif_vmm_pfnclr_v0));
	assert(!ret || (addr == reserved_addr && ret));
	if (eot)
		printf("\n\n\n");
}

static void
pfnmap(u64 _addr, u64 _size, u8 page, u64 *pfn, u64 *expected)
{
	const u64 addr = map_back ?
		  map_addr + ((map_wndw - _addr - _size) << page)
		: map_addr + (_addr << page);
	const u64 size = _size << page;
	struct {
		struct nvif_vmm_pfnmap_v0 pfnmap;
		u64 pfn[16];
	} args = {
		.pfnmap.page = page,
		.pfnmap.addr = addr,
		.pfnmap.size = size,
	};
	int ret;

	printf("pfnmap %d %016llx %016llx\n", page, addr, size);
	for (u64 i = 0; i < size; i += (1 << page))
		printf("       %016llx %016llx\n", addr + i, pfn[i >> page]);

	memcpy(args.pfn, pfn, sizeof(*pfn) * (size >> page));
	ret = nvif_object_mthd(&vmm.object, NVIF_VMM_V0_PFNMAP,
			       &args, sizeof(args.pfnmap) +
			       (size >> page) * sizeof(args.pfn[0]));
	assert(!ret || (addr == reserved_addr && ret));
	if (ret)
		return;

	if (!expected)
		expected = pfn;

	for (u64 i = 0; i < _size; i++) {
		printk(KERN_ERR "%016llx %016llx\n", expected[i], args.pfn[i]);
		assert((args.pfn[i] & NVIF_VMM_PFNMAP_V0_V) ==
		       (expected[i] & NVIF_VMM_PFNMAP_V0_V));
	}

	/*XXX: do GPU testing of expected page mapping... */
}

static void
test_vma(u8 page, u64 addr, bool back)
{
	struct vram vram;

	vram_get(&device, 1 << page, &vram, false, false);
	map_addr = addr;
	map_back = back;

	//XXX: map nvkm_memory into managed area
	memmap(0, 1, page, &vram.mem, false);
	memclr(0, 1, page, true);

	//XXX: pfnmap (single page)
	pfnmap(0, 1, page, (u64[]){ 0x13 }, NULL);
	pfnclr(0, 1, page, true);

	//XXX: pfnmap (multiple pages)
	pfnmap(0, 2, page, (u64[]){ 0x13, 0x13 }, NULL);
	pfnclr(0, 2, page, true);

	//XXX: pfnmap (multiple maps consecutive)
	pfnmap(0, 1, page, (u64[]){ 0x13 }, NULL);
	pfnmap(1, 1, page, (u64[]){ 0x13 }, NULL);
	pfnclr(0, 2, page, true);

	//XXX: pfnmap (multiple maps non-consecutive)
	pfnmap(0, 1, page, (u64[]){ 0x13 }, NULL);
	pfnmap(2, 1, page, (u64[]){ 0x13 }, NULL);
	pfnclr(0, 3, page, true);

	//XXX: pfnmap (full remap)
	pfnmap(0, 4, page, (u64[]){ 0x13, 0x13, 0x13, 0x13 }, NULL);
	pfnmap(0, 4, page, (u64[]){ 0x11, 0x11, 0x11, 0x11 }, NULL);
	pfnclr(0, 4, page, true);

	//XXX: pfnmap (part remap)
	pfnmap(0, 4, page, (u64[]){ 0x13, 0x13, 0x13, 0x13 }, NULL);
	pfnmap(0, 4, page, (u64[]){ 0x13, 0x11, 0x11, 0x13 }, NULL);
	pfnclr(0, 4, page, true);

	//XXX: pfnmap (head remap)
	pfnmap(2, 4, page, (u64[]){             0x13, 0x13, 0x13, 0x13 }, NULL);
	pfnmap(0, 3, page, (u64[]){ 0x11, 0x11, 0x11                   }, NULL);
	pfnclr(0, 6, page, true);

	//XXX: pfnmap (tail remap)
	pfnmap(0, 4, page, (u64[]){ 0x13, 0x13, 0x13, 0x13             }, NULL);
	pfnmap(3, 3, page, (u64[]){                   0x11, 0x11, 0x11 }, NULL);
	pfnclr(0, 6, page, true);

	//XXX: pfnmap (tail remap, case E)
	//XXX: maybe have "avoid nvkm_memory merging" tests for this
	pfnmap(0, 4, page, (u64[]){ 0x13, 0x13, 0x13, 0x13                   }, NULL);
	memmap(6, 1, page, &vram.mem, false);
	pfnmap(3, 3, page, (u64[]){                   0x11, 0x11, 0x11,      }, NULL);
	memclr(6, 1, page, false);
	pfnclr(0, 6, page, true);

	//XXX: pfnmap (full clear)
	pfnmap(0, 4, page, (u64[]){ 0x13, 0x13, 0x13, 0x13 }, NULL);
	pfnmap(0, 4, page, (u64[]){ 0x00, 0x00, 0x00, 0x00 }, NULL);
	pfnclr(0, 4, page, true);

	//XXX: pfnmap (part clear)
	pfnmap(0, 4, page, (u64[]){ 0x13, 0x13, 0x13, 0x13 }, NULL);
	pfnmap(0, 4, page, (u64[]){ 0x13, 0x00, 0x00, 0x13 }, NULL);
	pfnclr(0, 4, page, true);

	//XXX: pfnmap (head clear)
	pfnmap(2, 4, page, (u64[]){             0x13, 0x13, 0x13, 0x13 }, NULL);
	pfnmap(0, 3, page, (u64[]){ 0x11, 0x11, 0x00                   }, NULL);
	pfnclr(0, 6, page, true);

	//XXX: pfnmap (tail clear)
	pfnmap(0, 4, page, (u64[]){ 0x13, 0x13, 0x13, 0x13             }, NULL);
	pfnmap(3, 3, page, (u64[]){                   0x00, 0x11, 0x11 }, NULL);
	pfnclr(0, 6, page, true);

	//XXX: pfnclr (part)
	pfnmap(0, 3, page, (u64[]){ 0x13, 0x13, 0x13 }, NULL);
	pfnclr(1, 1, page, false);
	pfnclr(0, 3, page, true);

	//XXX: pfnclr (head)
	pfnmap(0, 3, page, (u64[]){ 0x13, 0x13, 0x13 }, NULL);
	pfnclr(0, 1, page, false);
	pfnclr(0, 3, page, true);

	//XXX: pfnclr (tail)
	pfnmap(0, 3, page, (u64[]){ 0x13, 0x13, 0x13 }, NULL);
	pfnclr(2, 1, page, false);
	pfnclr(0, 3, page, true);

	//XXX: pfnmap (tail overlaps nvkm_memory)
	memmap(2, 1, page, &vram.mem, false);
	pfnmap(0, 3, page, (u64[]){ 0x13, 0x13, 0x13 },
		map_back ? (u64[]){ 0x12, 0x13, 0x13 } :
			   (u64[]){ 0x13, 0x13, 0x12 });
	pfnclr(0, 3, page, false);
	memclr(2, 1, page, true);

	//XXX: pfnmap (fullly overlapping nvkm_memory)
	memmap(0, 1, page, &vram.mem, false);
	pfnmap(0, 1, page, (u64[]){ 0x13 },
			   (u64[]){ 0x12 });
	pfnclr(0, 1, page, false);
	memclr(0, 1, page, true);

	//XXX: pfnmap (head overlaps nvkm_memory)
	memmap(0, 1, page, &vram.mem, false);
	pfnmap(0, 3, page, (u64[]){ 0x13, 0x13, 0x13 },
		map_back ? (u64[]){ 0x13, 0x13, 0x12 } :
			   (u64[]){ 0x12, 0x13, 0x13 });
	pfnclr(0, 3, page, false);
	memclr(0, 1, page, true);

	//XXX: make sure nvkm_memory can't map over pfnmap!
	pfnmap(0, 2, page, (u64[]){ 0x13, 0x13 }, NULL);
	memmap(0, 1, page, &vram.mem, true);
	pfnclr(0, 2, page, true);

	vram_put(&device, &vram);
}

static void
test_vmm(void)
{
	struct nvif_vma vma;
	struct vram vram;
	int ret;

	//XXX: start of address-space
	test_vma(12, 0, false);
	//XXX: middle of address-space
	test_vma(12, 0x1000, false);
	//XXX: head of unmanaged area
	test_vma(12, reserved_addr - (map_wndw << 12), true);
	//XXX: tail of unmanaged area
	test_vma(12, reserved_addr + reserved_size, false);
	//XXX: end of address-space
	test_vma(12, vmm.limit - (map_wndw << 12), true);

	map_addr = 0;
	map_back = false;
	vram_get(&device, 0x1000, &vram, false, false);

	//XXX: no pfnmap in unallocated unmanaged area
	pfnmap(reserved_addr >> 12, 1, 12, (u64[]){ 0x13 },
					   (u64[]){ 0x12 });
	pfnclr(reserved_addr >> 12, 1, 12, true);

	//XXX: no pfnmap in allocated unmanaged area
	ret = nvif_vmm_get(&vmm, PTES, false, 12, 0, 0x1000, &vma);
	assert(ret == 0);
	pfnmap(vma.addr >> 12, 1, 12, (u64[]){ 0x13 },
				      (u64[]){ 0x12 });
	pfnclr(vma.addr >> 12, 1, 12, true);

	//XXX: no pfnmap in mapped unmanaged area
	ret = nvif_vmm_map(&vmm, vma.addr, 0x1000, NULL, 0, &vram.mem, 0);
	assert(ret == 0);
	pfnmap(vma.addr >> 12, 1, 12, (u64[]){ 0x13 },
				      (u64[]){ 0x12 });
	pfnclr(vma.addr >> 12, 1, 12, true);
	nvif_vmm_put(&vmm, &vma);

	vram_put(&device, &vram);
}

static bool chan_dead = false;
static int
chan_killed(struct nvif_notify *notify)
{
	printk(KERN_ERR "CHANNEL KILLED\n");
	chan_dead = true;
	return NVIF_NOTIFY_DROP;
}

static struct nvif_object rpfb;
static struct nvif_mem rpfb_wr;
static struct nvif_clb069_v0 rpfb_args;
static void *out;
static int
rpfb_process(struct nvif_notify *notify)
{
	int get = nvif_rd32(&device.object, rpfb_args.get);
	int put = nvif_rd32(&device.object, rpfb_args.put);
	if (put != get) {
		printk(KERN_ERR "RPFB GET %08x PUT %08x\n", get, put);
		u64 last_addr = ~0ULL;
		for (; get != put; get++) {
			const u32 instlo = nvif_rd32(&rpfb, (get * 32) + 0x00);
			const u32 insthi = nvif_rd32(&rpfb, (get * 32) + 0x04);
			const u32 addrlo = nvif_rd32(&rpfb, (get * 32) + 0x08);
			const u32 addrhi = nvif_rd32(&rpfb, (get * 32) + 0x0c);
			const u32 timelo = nvif_rd32(&rpfb, (get * 32) + 0x10);
			const u32 timehi = nvif_rd32(&rpfb, (get * 32) + 0x14);
			const u32   rsvd = nvif_rd32(&rpfb, (get * 32) + 0x18);
			const u32   info = nvif_rd32(&rpfb, (get * 32) + 0x1c);
			const u32  valid = (info & 0x80000000) >> 31;
			const u32    gpc = (info & 0x1f000000) >> 24;
			const u32  isgpc = (info & 0x00100000) >> 20;
			const u32 access = (info & 0x00070000) >> 16;
			const u32 client = (info & 0x00007f00) >> 8;
			const u32  fault = (info & 0x0000001f) >> 0;
			const u64   addr = (u64)addrhi << 32 | addrlo;
			int ret;

			printk(KERN_ERR "%08x: \t%08x\n", get, instlo);
			printk(KERN_ERR "\t\t%08x\n", insthi);
			printk(KERN_ERR "\t\t%016llx\n", addr);
			printk(KERN_ERR "\t\t%08x\n", timelo);
			printk(KERN_ERR "\t\t%08x\n", timehi);
			printk(KERN_ERR "\t\t%08x\n",   rsvd);
			printk(KERN_ERR "\t\t%08x\n",   info);
			(void)fault; (void)access; (void)client;
			(void)isgpc; (void)gpc;

			printf("%x %x\n", get, rpfb_args.entries);
			if (get == rpfb_args.entries - 1)
				get = -1;
			nvif_wr32(&device.object, rpfb_args.get, get + 1);
			if (!valid)
				continue;

			nvif_mask(&rpfb, 0x1c, 0x80000000, 0x00000000);

			if (addr == last_addr)
				continue;
			last_addr = addr;

			struct nvif_mem tmp, *mem = &tmp;
			printk(KERN_ERR "access %d\n", access);
			if (addr == (unsigned long)out)
				mem = &rpfb_wr;

			ret = nvif_mem_init(&mmu, NVIF_CLASS_MEM_GF100,
					    NVIF_MEM_VRAM |
					    NVIF_MEM_MAPPABLE |
					    NVIF_MEM_COHERENT,
					    0, 4096, NULL, 0, mem);
			assert(ret == 0);
			ret = nvif_object_map(&mem->object, NULL, 0);
			assert(ret == 0);
			if (access == 0)
				memcpy(mem->object.map.ptr, (void *)addr, 4096);
			ret = nvif_vmm_map(&vmm, addr, 4096, NULL, 0, mem, 0);
			assert(ret == 0);
			if (addr != (unsigned long)out)
				nvif_mem_fini(mem);
		}
		nvif_object_mthd(&vmm.object, GP100_VMM_VN_FAULT_REPLAY,
				 &(struct gp100_vmm_fault_replay_vn) {},
				 sizeof(struct gp100_vmm_fault_replay_vn));
	}
	return NVIF_NOTIFY_KEEP;
}

struct nvif_object chan;
static u32 push_pbput = 0;
static u32 push_pbcur = 0;
static struct vram push;

static void
DATA(u32 data) {
	printk(KERN_ERR "%08x: %08x\n", push_pbcur * 4, data);
	((u32 *)push.mem.object.map.ptr)[push_pbcur++] = data;
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
	u64 addr = push.vma.addr + (push_pbput * 4);
	u32 put = nvif_rd32(&chan, 0x8c);
	u32 *ib = (u32 *)((u8 *)push.mem.object.map.ptr + 0x10000 + (put * 8));
	ib[0] = lower_32_bits(addr);
	ib[1] = upper_32_bits(addr) | ((push_pbcur - push_pbput) << 10);
	nvif_wr32(&chan, 0x8c, ++put);
	push_pbput = push_pbcur;
	printk(KERN_ERR "KICK %08x%08x (%d)\n", ib[1], ib[0], put);
	if (device.user.func)
		device.user.func->doorbell(&device.user, 0);
}

static void
IDLE(void)
{
	u32 refcnt = rand();
	INCR(0, 0x50, 1);
	DATA(refcnt);
	KICK();
	if (nvif_msec(&device, 2000,
		if (nvif_rd32(&chan, 0x48) == refcnt)
			break;
	) < 0)
		assert(0);
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

#define PARM_SIZE (1 << 16)

static struct vram desc;
static void
test(s32 oclass, void *code)
{
	out = aligned_alloc(4096, 1 << 20);
	u64 addr = (unsigned long)out;
	u32 value;

	printk(KERN_ERR "buffer @ %016llx\n", addr);
	srand(time(NULL));
	value = rand();

	/*XXX: double-check launch dma, not sure if it can replay or not. */
	struct {
		u32 addrhi;
		u32 addrlo;
		u32 data;
	} *parm = aligned_alloc(4096, PARM_SIZE);
	parm->addrhi = upper_32_bits(addr);
	parm->addrlo = lower_32_bits(addr);
	parm->data = value;

	if (oclass < VOLTA_COMPUTE_A) {
		struct gp100_cp_launch_desc *d = desc.mem.object.map.ptr;
		memset(d, 0x00, sizeof(*d));
		d->unk0[4]  = 0x40;
		d->unk11_0  = 0x04014000;
		d->entry = 0;
		d->griddim_x = 1;
		d->griddim_y = 1;
		d->griddim_z = 1;
		d->blockdim_x = 1;
		d->blockdim_y = 1;
		d->blockdim_z = 1;
		d->shared_size = 0;
		d->local_size_p = 0;
		d->local_size_n = 0;
		d->cstack_size = 0x800;
		d->gpr_alloc = 3;
		d->bar_alloc = 0;
		d->cb[0].address_l = lower_32_bits((unsigned long)parm);
		d->cb[0].address_h = upper_32_bits((unsigned long)parm);
		d->cb[0].size_sh4 = DIV_ROUND_UP(PARM_SIZE, 16);
		d->cb_mask = 1;
	} else {
		struct gv100_cp_launch_desc *d = desc.mem.object.map.ptr;
		memset(d, 0x00, sizeof(*d));
		d->entry = (unsigned long)code;
		d->griddim_x = 1;
		d->griddim_y = 1;
		d->griddim_z = 1;
		d->blockdim_x = 1;
		d->blockdim_y = 1;
		d->blockdim_z = 1;
		d->gpr_alloc = 5;
		d->cb[0].address_l = lower_32_bits((unsigned long)parm);
		d->cb[0].address_h = upper_32_bits((unsigned long)parm);
		d->cb[0].size_sh4 = DIV_ROUND_UP(PARM_SIZE, 16);
		d->cb_mask = 0x1;
	}

	INCR(1, 0x1698, 1);
	DATA(0x00001000);
	INCR(1, 0x021c, 1);
	DATA(0x00001017);
	if (oclass < VOLTA_COMPUTE_A) {
		INCR(1, 0x0274, 3);
		DATA(upper_32_bits((unsigned long)parm));
		DATA(lower_32_bits((unsigned long)parm));
		DATA(0x80000010);
	}

	INCR(1, 0x02b4, 1);
	DATA(desc.vma.addr >> 8);
	INCR(1, 0x02bc, 1);
	DATA(0x00000003);
	INCR(1, 0x0110, 1);
	DATA(0x00000000);
	KICK();

	IDLE();
	if (!rpfb_wr.object.client || // << can happen if we never process
			              //    shader writes (ie. ILL_INSTR_ENC)
	    ((u32 *)rpfb_wr.object.map.ptr)[0] != value) {
		fprintf(stderr, "FAIL!!!\n");
//		assert(0);
	}
	nvif_mem_fini(&rpfb_wr);
}

static u32 kernel_sm60[] = {
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
	0x00070002,
	0xbf900000,
	0x0007000f,
	0xe3000000,
	0x0007000f,
	0xe3000000,
};

static u32 kernel_sm70[] = {
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
	0x00007385,
	0x00000000,
	0x00114902,
	0x000fc000,
	0x0000794d,
	0x00000000,
	0x03800000,
	0x000fc000,
};

int
main(int argc, char **argv)
{
	static const struct nvif_mclass mmus[] = {
		{ NVIF_CLASS_MMU_GF100, -1 },
		{}
	};
	static const struct nvif_mclass vmms[] = {
		{ NVIF_CLASS_VMM_GP100, -1 },
		{}
	};
	static const struct nvif_mclass rpfbs[] = {
		{   VOLTA_FAULT_BUFFER_A, 0 },
		{ MAXWELL_FAULT_BUFFER_A, 0 },
		{}
	};
	static const struct nvif_mclass fifos[] = {
		{  VOLTA_CHANNEL_GPFIFO_A, 0 },
		{ PASCAL_CHANNEL_GPFIFO_A, 0 },
		{}
	};
	static const struct nvif_mclass computes[] = {
		{  VOLTA_COMPUTE_A, -1 },
		{ PASCAL_COMPUTE_B, -1 },
		{ PASCAL_COMPUTE_A, -1 },
		{}
	};
	struct nvif_client client;
	int ret, c;

	while ((c = getopt(argc, argv, U_GETOPT)) != -1) {
		switch (c) {
		default:
			if (!u_option(c))
				return 1;
			break;
		}
	}

	ret = u_device(NULL, argv[0], "info", true, true, ~0ULL,
		       0x00000000, &client, &device);
	if (ret)
		return ret;

	/* Reserve a chunk of (low) address-space for channel buffers. */
	reserved_size = 128 * 1024 * 1024;
	reserved_addr = reserve_vma(reserved_size, true);
	assert(reserved_addr);

	/* Allocate MMU. */
	ret = nvif_mclass(&device.object, mmus);
	assert(ret >= 0);
	ret = nvif_mmu_init(&device.object, mmus[ret].oclass, &mmu);
	assert(ret == 0);

	/* Allocate VMM. */
	ret = nvif_mclass(&mmu.object, vmms);
	assert(ret >= 0);
	ret = nvif_vmm_init(&mmu, vmms[ret].oclass, true, reserved_addr,
			    reserved_size, &(struct gp100_vmm_v0) {
				.fault_replay = true,
			    }, sizeof(struct gp100_vmm_v0), &vmm);
	assert(ret == 0);

	test_vmm();
	if (0)
		goto done_vmm;

	/* Allocate replayable fault buffer. */
	ret = nvif_mclass(&device.object, rpfbs);
	assert(ret >= 0);
	ret = nvif_object_init(&device.object, 0, rpfbs[ret].oclass,
			       &rpfb_args, sizeof(rpfb_args), &rpfb);
	assert(ret == 0);
	nvif_object_map(&rpfb, NULL, 0);

	/* Request notification of pending replayable faults. */
	struct nvif_notify pending;
	ret = nvif_notify_init(&rpfb, rpfb_process, true, NVB069_V0_NTFY_FAULT,
			       NULL, 0, 0, &pending);
	assert(ret == 0);
	ret = nvif_notify_get(&pending);
	assert(ret == 0);

	/* Allocate push buffer. */
	vram_get(&device, 0x11000, &push, true, true);

	/* Allocate channel. */
	u64 runmgr = nvif_fifo_runlist(&device, NV_DEVICE_INFO_ENGINE_GR);
	ret = nvif_mclass(&device.object, fifos);
	assert(ret >= 0);
	ret = nvif_object_init(&device.object, 0, fifos[ret].oclass,
			       &(struct kepler_channel_gpfifo_a_v0) {
				.ilength = 0x1000,
				.ioffset = push.vma.addr + 0x10000,
				.runlist = runmgr,
				.vmm = nvif_handle(&vmm.object),
			       }, sizeof(struct kepler_channel_gpfifo_a_v0),
			       &chan);
	assert(ret == 0);
	nvif_object_map(&chan, NULL, 0);

	if (chan.oclass >= VOLTA_CHANNEL_GPFIFO_A) {
		ret = nvif_user_init(&device);
		assert(ret == 0);
	}

	/* Request notification of channel death (for non-replayable fault). */
	struct nvif_notify killed;
	ret = nvif_notify_init(&chan, chan_killed, true, NVA06F_V0_NTFY_KILLED,
			       NULL, 0, 0, &killed);
	assert(ret == 0);
	ret = nvif_notify_get(&killed);
	assert(ret == 0);

	/* Test channel is working. */
	IDLE();
	IDLE();

	/* Allocate compute, and test subchannel bind. */
	struct nvif_object compute;
	ret = nvif_mclass(&chan, computes);
	assert(ret >= 0);
	ret = nvif_object_init(&chan, 0, computes[ret].oclass,
			       NULL, 0, &compute);
	assert(ret == 0);

	INCR(1, 0x0000, 1);
	DATA(compute.oclass);
	KICK();
	IDLE();

	/* Setup compute resources. */
	const int mp_count = 16;
	vram_get(&device,  1 << 16, &desc, true, true); /* no replayable fault? */

#define TEMP_SIZE ((u64)(16 << 20))

	void *temp = aligned_alloc(4096, TEMP_SIZE);
	void *code = aligned_alloc(4096,   1 << 20);
	if (compute.oclass < VOLTA_COMPUTE_A)
		memcpy(code, kernel_sm60, sizeof(kernel_sm60));
	else
		memcpy(code, kernel_sm70, sizeof(kernel_sm70));

	INCR(1, 0x0790, 2);
	DATA(upper_32_bits((unsigned long)temp));
	DATA(lower_32_bits((unsigned long)temp));
	INCR(1, 0x02e4, 3);
	DATA(upper_32_bits(TEMP_SIZE / mp_count));
	DATA(lower_32_bits(TEMP_SIZE / mp_count) & ~0x7fff);
	DATA(0x000000ff);
	if (compute.oclass < VOLTA_COMPUTE_A) {
		INCR(1, 0x02f0, 3);
		DATA(upper_32_bits(TEMP_SIZE / mp_count));
		DATA(lower_32_bits(TEMP_SIZE / mp_count) & ~0x7fff);
		DATA(0x000000ff);
		INCR(1, 0x077c, 1);
		DATA(0xff000000);
		INCR(1, 0x0214, 1);
		DATA(0xfe000000);
		INCR(1, 0x1608, 2);
		DATA(upper_32_bits((unsigned long)code));
		DATA(lower_32_bits((unsigned long)code));
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
	IDLE();

	/* Test replayable fault. */
	test(compute.oclass, code);

	/* Cleanup! */
	printf("shutting down...\n");
	vram_put(&device, &desc);
	nvif_object_fini(&compute);
	nvif_notify_fini(&killed);
	nvif_object_fini(&chan);
	vram_put(&device, &push);
	nvif_notify_fini(&pending);
	nvif_object_fini(&rpfb);
done_vmm:
	nvif_vmm_fini(&vmm);
	nvif_mmu_fini(&mmu);
	nvif_device_fini(&device);
	nvif_client_fini(&client);
	printf("done!\n");
	return ret;
}

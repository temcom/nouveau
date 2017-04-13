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
#include <core/sink.h>

struct nvkm_sink_item {
	struct list_head head;
	unsigned name:31;
	bool valid:1;
	u32 addr;
	u32 data;
};

void
nvkm_sink_nsec(struct nvkm_sink *sink, u64 nsec)
{
	sink->func->nsec(sink, nsec);
}

void
nvkm_sink_wait(struct nvkm_sink *sink, u32 name, u32 mask, u32 data, u64 nsec)
{
	if (sink->parent) {
		struct nvkm_sink_item *item = nvkm_sink_name(sink, name, name);
		if (name)
			sink->func->wait(sink, item->addr, mask, data, nsec);
	} else {
		sink->func->wait(sink, name, mask, data, nsec);
	}
}

void
nvkm_sink_wr32(struct nvkm_sink *sink, u32 name, u32 data)
{
	if (sink->parent) {
		struct nvkm_sink_item *item = nvkm_sink_name(sink, name, name);
		if (name) {
			item->valid = true;
			item->data = data;
			sink->func->wr32(sink, item->addr, data);
		}
	} else {
		sink->func->wr32(sink, name, data);
	}
}

u32
nvkm_sink_rd32(struct nvkm_sink *sink, u32 name)
{
	struct nvkm_sink *parent = sink->parent;
	if (parent) {
		struct nvkm_sink_item *item = nvkm_sink_name(sink, name, name);
		if (name) {
			if (!item->valid) {
				item->data = nvkm_sink_rd32(parent, item->addr);
				item->valid = true;
			}
			return item->data;
		}
		return 0;
	}
	return sink->func->rd32(sink, name);
}

struct nvkm_sink_item *
nvkm_sink_have(struct nvkm_sink *sink, u32 name)
{
	struct nvkm_sink_item *item;

	list_for_each_entry(item, &sink->items, head) {
		if (item->name == name) {
			list_del(&item->head);
			list_add(&item->head, &sink->items);
			return item;
		}
	}

	return NULL;
}

struct nvkm_sink_item *
nvkm_sink_name(struct nvkm_sink *sink, u32 name, u32 addr)
{
	struct nvkm_sink_item *item = nvkm_sink_have(sink, name);
	if (!item) {
		item = kmalloc(sizeof(*item), GFP_KERNEL);
		if (!WARN_ON(!name)) {
			list_add(&item->head, &sink->items);
			item->name = name;
			item->addr = addr;
			item->valid = false;
		}
	}
	return item;
}

void
nvkm_sink_dtor(struct nvkm_sink *sink)
{
	struct nvkm_sink_item *item, *itmp;
	list_for_each_entry_safe(item, itmp, &sink->items, head) {
		list_del(&item->head);
		kfree(item);
	}
}

void
nvkm_sink_ctor(const struct nvkm_sink_func *func,
	       struct nvkm_sink *parent, struct nvkm_sink *sink)
{
	sink->func = func;
	sink->parent = parent;
	INIT_LIST_HEAD(&sink->items);
}

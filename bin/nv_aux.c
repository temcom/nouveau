#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

#include <nvif/client.h>
#include <nvif/device.h>
#include <nvif/class.h>

#include "util.h"

static void
print_aux(struct nvkm_i2c_aux *aux)
{
	printf("aux %04x\n", aux->id);
}

int
main(int argc, char **argv)
{
	struct nvif_client client;
	struct nvif_device device;
	struct nvkm_i2c_aux *aux;
	struct nvkm_i2c *i2c;
	int action = -1, index = -1;
	int addr = -1, val = -1;
	int ret, c;
	u8 data;

	while ((c = getopt(argc, argv, "-"U_GETOPT)) != -1) {
		switch (c) {
		case 1:
			if (action < 0) {
				if (!strcasecmp(optarg, "rd"))
					action = 0;
				else
				if (!strcasecmp(optarg, "wr"))
					action = 1;
				else
					return -EINVAL;
			} else
			if (action >= 0 && index < 0) {
				index = strtoul(optarg, NULL, 0);
			} else
			if (action >= 0 && addr < 0) {
				addr = strtoul(optarg, NULL, 0);
				if (addr > 0x000fffff)
					return -EINVAL;
			} else
			if (action >= 1 && val < 0) {
				val = strtoul(optarg, NULL, 0);
				if (val > 0xff)
					return -EINVAL;
				data = val;
			} else
				return -EINVAL;
			break;
		default:
			if (!u_option(c))
				return 1;
			break;
		}
	}

	ret = u_device("lib", argv[0], "error", true, true,
		       (1ULL << NVKM_SUBDEV_PCI) |
		       (1ULL << NVKM_SUBDEV_VBIOS) |
		       (1ULL << NVKM_SUBDEV_I2C),
		       0x00000000, &client, &device);
	if (ret)
		return ret;

	i2c = nvxx_i2c(&device);

	if (action < 0) {
		list_for_each_entry(aux, &i2c->aux, head) {
			print_aux(aux);
		}
	} else {
		aux = nvkm_i2c_aux_find(i2c, index);
		if (!aux) {
			ret = -ENOENT;
			goto done;
		}

		print_aux(aux);
	}

	switch (action) {
	case 0:
		ret = nvkm_rdaux(aux, addr, &data, 1);
		printf("%05x: ", addr);
		if (ret < 0)
			printf("%s\n", strerror(ret));
		else
			printf("%02x\n", data);
		break;
	case 1:
		printf("%05x: %02x", addr, data);
		ret = nvkm_wraux(aux, addr, &data, 1);
		if (ret < 0)
			printf(" - %s", strerror(ret));
		printf("\n");
		break;
	}

done:
	nvif_device_fini(&device);
	nvif_client_dtor(&client);
	return ret;
}

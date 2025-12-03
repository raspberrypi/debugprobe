#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include <libusb-1.0/libusb.h>
#include "remote_gpio.h"


#define VENDOR_ID	  0x2E8A   // Raspberry Pi
#define PRODUCT_ID	 0x000c   // Debugprobe
#define BCDD_VER	0x0103
#define NR_GPIOS	   28 // Limit to RP2040/2350A for now
#define GPIO_ALL	0xff

#define GET_STATE BMREQUEST_GPIO_GET
#define SET_STATE BMREQUEST_GPIO_SET

#define GPIO_OUT 1
#define GPIO_IN 0

static const char usage[] = {
	"Use: \n"
	"  probe-gpio <get> [GPIO]\n"
	"  probe-gpio <set> <GPIO> <OPTIONS>\n"
	"\n"
	"  get: retrieve GPIO state\n"
	"  if 'get' is specified with no further arguments, then the state of all gpios is returned.\n"
	"\n"
	"  set: GPIO must be specified. One at a time, unlike raspi-gpio.\n"
	"  OPTIONS: one of:\n"
	"  op - drive GPIO output\n"
	"  ip - drive GPIO input\n"
	"  dl - drive low\n"
	"  dh - drive high\n"
	"  pu - pull-up\n"
	"  pd - pull-down\n"
	"  pn - pull none\n"
};

const char* pull_state[] = {
	"NONE",
	"UP",
	"DOWN",
	"KEEPER"
};

const char* dir_state[] = {
	"INPUT",
	"OUTPUT",
};

static void print_gpio_state(int gpio, int level, int function, unsigned int dir, unsigned int pull)
{
	fprintf(stdout, "GPIO %d: level=%d function=%d dir=%s pull=%s\n",
		gpio, level, function,
		dir <= 1 ? dir_state[dir] : "UNK",
		pull <= 3 ? pull_state[pull] : "UNK"
		);
};

int main(int argc, char **argv)
{
	int rc, i = 0;
	struct libusb_device_handle *devh = NULL;
	struct libusb_device **dlist = NULL;
	struct libusb_context *ctx = NULL;
	struct libusb_device_descriptor desc = {0};
	int op;
	int gpio;
	int set_fn = -1;
	unsigned char dummy[4];
	unsigned char data[4] = {};
	uint32_t glevel, gfunction, gdir, gpull;
	
	if (argc < 2 || argc > 4) {
		fprintf(stderr, "Need an operation to do.\n");
		fprintf(stderr, "%s", usage);
		return -1;
	}
	if (strcmp(argv[1], "get") == 0) {
		op = BMREQUEST_GPIO_GET;
		if (argc == 2)
			gpio = GPIO_ALL;
		else {
			gpio = strtol(argv[2], NULL, 10);
			if (gpio < 0 || gpio > 28) {
				fprintf(stderr, "gpio out of range: %d\n", gpio);
				return errno;
			}
		}
	} else if (strcmp(argv[1], "set") == 0) {
		op = BMREQUEST_GPIO_SET;
		if (argc != 4) {
			fprintf(stderr, "need to specify gpio and operation\n");
			fprintf(stderr, "%s", usage);
			return -1;
		}
		gpio = strtol(argv[2], NULL, 10);
		if (gpio < 0 || gpio > 29) {
			fprintf(stderr, "gpio out of range: %d\n", gpio);
			return errno;
		}
		if (strcmp(argv[3], "op") == 0) {
			set_fn = GPIO_SET_DIR;
			data[0] = GPIO_OUT;
		} else if (strcmp(argv[3], "ip") == 0) {
			set_fn = GPIO_SET_DIR;
			data[0] = GPIO_IN;
		} else if (strcmp(argv[3], "dl") == 0) {
			set_fn = GPIO_PUT;
			data[0] = 0;
		} else if (strcmp(argv[3], "dh") == 0) {
			set_fn = GPIO_PUT;
			data[0] = 1;
		} else if (strcmp(argv[3], "pu") == 0) {
			set_fn = GPIO_SET_PULLS;
			data[0] = 1;
		} else if (strcmp(argv[3], "pd") == 0) {
			set_fn = GPIO_SET_PULLS;
			data[0] = 1 << 1;
		} else if (strcmp(argv[3], "pn") == 0) {
			set_fn = GPIO_SET_PULLS;
			data[0] = 0;
		} else {
			fprintf(stderr, "invalid operation specified\n");
			fprintf(stderr, "%s", usage);
			return -1;
		}
	} else {
		fprintf(stderr, "Operation must be <set> or <get>\n");
		fprintf(stderr, "%s", usage);
		return -1;
	}
	 
	/* Initialize libusb
	 */
	rc = libusb_init(&ctx);
	if (rc < 0) {
		fprintf(stderr, "Error initializing libusb: %s\n", libusb_error_name(rc));
		exit(rc);
	}

	/* Set debugging output to max level.
	 */
	#if LIBUSB_API_VERSION >= 0x01000106
	libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 3);
	#else
	libusb_set_debug(ctx, 3);
	#endif
	rc = libusb_get_device_list(ctx, &dlist);
	if (rc < 0) {
		fprintf(stderr, "Error retrieving device list: %s\n", libusb_error_name(i));
		exit(rc);
	}
	

	/* Look for a specific device and open it.
	 */
	for (i = 0; i < rc; i++) {
		libusb_device *device = dlist[i];
		libusb_get_device_descriptor(device, &desc);
		if (desc.idVendor == VENDOR_ID && desc.idProduct == PRODUCT_ID) {
			//if(desc.bcdDevice == BCDD_VER) {
				//printf(stderr, "Found a Debug Probe version %04x - using it\n", desc.bcdDevice);
				break;
		//	} else {
				//fprintf(stderr, "Found a Debug Probe version %04x - incompatible\n", desc.bcdDevice);
		//	}
		}
	}
	if (i == rc) {
		fprintf(stderr, "Error: no compatible Debug Probes found - wrong fw?\n");
		goto out;
	}

	rc = libusb_open(dlist[i], &devh);
	if (rc < 0) {
		fprintf(stderr, "Error: can't open device at address %u: %s\n", libusb_get_device_address(dlist[i]), libusb_error_name(rc));
		goto out;
	}

	libusb_free_device_list(dlist, 1);

	/* Does the it do remote gpio? */
	rc = libusb_control_transfer(devh, BMREQUEST_GPIO_GET, CTRL_REMOTE_GPIO_REQ, GPIO_GET_FUNCTION,
								0, (unsigned char *)&dummy, sizeof(dummy), 0);
	if (rc < 0) {
		fprintf(stderr, "Error: probe doesn't understand REMOTE_GPIO access - wrong fw?\n");
		goto out;
	}
	rc = 0;

	if (op == BMREQUEST_GPIO_GET) {
		if (gpio == GPIO_ALL) {
			for (i = 0; i < NR_GPIOS; i++) {
				rc = libusb_control_transfer(devh, op, CTRL_REMOTE_GPIO_REQ, GPIO_GET,
								i, (unsigned char *)&glevel, sizeof(glevel), 3000); if (rc < 0) goto out;
				rc = libusb_control_transfer(devh, op, CTRL_REMOTE_GPIO_REQ, GPIO_GET_FUNCTION,
								i, (unsigned char *)&gfunction, sizeof(gfunction), 3000); if (rc < 0) goto out;
				rc = libusb_control_transfer(devh, op, CTRL_REMOTE_GPIO_REQ, GPIO_GET_DIR,
								i, (unsigned char *)&gdir, sizeof(gdir), 3000); if (rc < 0) goto out;
				rc = libusb_control_transfer(devh, op, CTRL_REMOTE_GPIO_REQ, GPIO_GET_PULLS,
								i, (unsigned char *)&gpull, sizeof(gpull), 3000); if (rc < 0) goto out;
				print_gpio_state(i, glevel, gfunction, gdir, gpull);
			}
		} else {
			rc = libusb_control_transfer(devh, op, CTRL_REMOTE_GPIO_REQ, GPIO_GET,
						gpio, (unsigned char *)&glevel, sizeof(glevel), 3000); if (rc < 0) goto out;
			rc = libusb_control_transfer(devh, op, CTRL_REMOTE_GPIO_REQ, GPIO_GET_FUNCTION,
						gpio, (unsigned char *)&gfunction, sizeof(gfunction), 3000); if (rc < 0) goto out;
			rc = libusb_control_transfer(devh, op, CTRL_REMOTE_GPIO_REQ, GPIO_GET_DIR,
						gpio, (unsigned char *)&gdir, sizeof(gdir), 3000); if (rc < 0) goto out;
			rc = libusb_control_transfer(devh, op, CTRL_REMOTE_GPIO_REQ, GPIO_GET_PULLS,
						gpio, (unsigned char *)&gpull, sizeof(gpull), 3000); if (rc < 0) goto out;
			print_gpio_state(gpio, glevel, gfunction, gdir, gpull);
		}
	} else if (op == BMREQUEST_GPIO_SET) {
		// Need to init a gpio before SIO can do anything useful to it. This clobbers output enable, so avoid glitches
		rc = libusb_control_transfer(devh, BMREQUEST_GPIO_GET, CTRL_REMOTE_GPIO_REQ, GPIO_GET_FUNCTION,
								gpio, (unsigned char *)dummy, 4, 300); if (rc < 0) goto out;
		if (dummy[0] != 5) {
			rc = libusb_control_transfer(devh, op, CTRL_REMOTE_GPIO_REQ, GPIO_INIT,
							gpio, (unsigned char *)data, 4, 3000); if (rc < 0) goto out;
			//printf("fn was %d, setting init\n", gfunction);
		}
		rc = libusb_control_transfer(devh, op, CTRL_REMOTE_GPIO_REQ, set_fn,
						gpio, (unsigned char *)data, 4, 3000); if (rc < 0) goto out;
	}

	libusb_release_interface(devh, 0);

out:
	if (rc < 0) {
		fprintf(stderr, "libusb error: %s\n", libusb_error_name(rc));
	}
	if (devh)
		libusb_close(devh);
	libusb_exit(NULL);
	return rc < 0 ? rc : 0;
}

/**
 * lib/cvorg.c
 *
 * CVORG library
 * Copyright (c) 2010 Emilio G. Cota <cota@braap.org>
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <skeluser.h>
#include <skeluser_ioctl.h>
#include <skel.h>

#include <libad9516.h>

#include <libcvorg.h>
#include "libinternal.h"

#define LIBCVORG_VERSION	"1.0"

const char *libcvorg_version = LIBCVORG_VERSION;

/* lynx needs this */
extern int snprintf(char *s, size_t n, const char *format, ...);

int __cvorg_init;

static void __cvorg_initialize(void)
{
	char *value;

	__cvorg_init = 1;

	value = getenv("LIBCVORG_LOGLEVEL");
	if (value) {
		__cvorg_loglevel = strtol(value, NULL, 0);
		LIBCVORG_DEBUG(3, "Setting loglevel to %d\n", __cvorg_loglevel);
	}
}

static int __cvorg_open(struct cvorglib *device, int devicenr)
{
	char devfile[32];
	int i;

	for (i = 1;; i++) {
		snprintf(devfile, 31, "/dev/cvorg.%d", i);
		devfile[31] = '\0';

		if (access(devfile, F_OK))
			return -1;

		device->fd = open(devfile, O_RDWR, 0);
		if (device->fd > 0)
			return 0;
	}
}

/**
 * @brief Open a CVORG device handle
 * @param devicenr	- Number of the device to open (from 1 to n)
 * @param channelnr	- Channel number (enum)
 *
 * @return Pointer to the opened file handle on success, NULL on failure
 */
cvorg_t *cvorg_open(int devicenr, enum cvorg_channelnr channelnr)
{
	int ret;
	cvorg_t *device;

	if (!__cvorg_init)
		__cvorg_initialize();
	LIBCVORG_DEBUG(4, "opening device %d channel %s\n", devicenr,
		channelnr == CVORG_CHANNEL_A ? "A" : "B");

	device = malloc(sizeof(*device));
	if (device == NULL) {
		__cvorg_libc_error(__func__);
		return NULL;
	}
	memset(device, 0, sizeof(*device));

	/* open the file descriptor */
	ret = __cvorg_open(device, devicenr);
	if (ret) {
		__cvorg_libc_error(__func__);
		goto out_free;
	}

	/* and now set the module */
	ret = ioctl(device->fd, SkelDrvrIoctlSET_MODULE, &devicenr);
	if (ret < 0) {
		__cvorg_libc_error(__func__);
		goto out_free;
	}

	/* and the channel */
	ret = ioctl(device->fd, CVORG_IOCSCHANNEL, &channelnr);
	if (ret < 0) {
		__cvorg_libc_error(__func__);
		goto out_free;
	}

	LIBCVORG_DEBUG(4, "opened device %d channel %s on fd %d\n", devicenr,
		channelnr == CVORG_CHANNEL_A ? "A" : "B", device->fd);
	return device;
 out_free:
	free(device);
	return NULL;
}

/**
 * @brief Close a CVORG handle
 * @param device	- CVORG device handle
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_close(cvorg_t *device)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	ret = close(device->fd);
	if (ret < 0)
		__cvorg_libc_error(__func__);

	free(device);

	return ret;
}

/**
 * @brief Claim ownership of the device
 * @param device	- CVORG device handle
 *
 * @return 0 on success, -1 on failure
 *
 * After calling this function, other cvorg_t handles (obtained via
 * cvorg_open()) from anywhere in the system won't be able to change the
 * status of the device, although they will still be able to read it.
 * In other words, only 'cvorg_get_*' calls will work through these other
 * handles.
 *
 * To unlock the device, cvorg_unlock() shall be called.
 *
 * When a process gets killed, or when cvorg_close() is called, the ownership
 * of the device is reset to nobody, which is the default state.
 */
int cvorg_lock(cvorg_t *device)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	ret = ioctl(device->fd, CVORG_IOCTLOCK, NULL);
	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Relinquish the ownership of the device
 * @param device	- CVORG device handle
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_unlock(cvorg_t *device)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	ret = ioctl(device->fd, CVORG_IOCTUNLOCK, NULL);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Set the sampling frequency of the device
 * @param device	- CVORG device handle
 * @param freq		- Desired frequency, in Hz
 *
 * @return 0 on success, -1 on failure
 *
 * This frequency is generated by tuning an internal oscillator. An external
 * clock can be used as well by setting freq to 0.
 *
 * This function puts the calling process to sleep until the configured
 * frequency is available at the output of the module. Note that this
 * sleeping wait may last for a few hundreds of milliseconds.
 *
 * Changing the sampling frequency of a device with any of its channels
 * in 'busy' state might fail.
 */
int cvorg_set_sampfreq(cvorg_t *device, unsigned int freq)
{
	struct ad9516_pll pll;
	int ret;

	LIBCVORG_DEBUG(4, "fd %d freq %d\n", device->fd, freq);
	if (freq > CVORG_MAX_FREQ) {
		LIBCVORG_DEBUG(2, "Invalid frequency (%d Hz). Max: %d Hz\n",
			freq, CVORG_MAX_FREQ);
		__cvorg_errno = LIBCVORG_EINVAL_FREQ;
		return -1;
	}

	if (ad9516_fill_pll_conf(freq, &pll)) {
		__cvorg_internal_error(LIBCVORG_EINVAL_FREQ);
		return -1;
	}

	ret = ioctl(device->fd, CVORG_IOCSPLL, &pll);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Get the current sampling frequency of the device
 * @param device	- CVORG device handle
 * @param freq		- Retrieved frequency, in Hz
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_get_sampfreq(cvorg_t *device, unsigned int *freq)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	ret = ioctl(device->fd, CVORG_IOCGSAMPFREQ, freq);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Set the analog output offset of the current channel
 * @param device	- CVORG device handle
 * @param outoff	- Desired analog offset at the output
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_set_outoff(cvorg_t *device, enum cvorg_outoff outoff)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d offset %s\n", device->fd,
		outoff == CVORG_OUT_OFFSET_0V ? "0V" : "2.5V");
	ret = ioctl(device->fd, CVORG_IOCSOUTOFFSET, &outoff);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Get the analog output offset of the current channel
 * @param device	- CVORG device handle
 * @param outoff	- Retrieved analog output offset
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_get_outoff(cvorg_t *device, enum cvorg_outoff *outoff)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	ret = ioctl(device->fd, CVORG_IOCGOUTOFFSET, outoff);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Set the analog output gain of the current channel
 * @param device	- CVORG device handle
 * @param outgain	- Desired analog gain at the output, in dB
 *
 * The value of @param outgain may be rounded when calling this function.
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_set_outgain(cvorg_t *device, int32_t *outgain)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d outgain %d\n", device->fd, *outgain);
	ret = ioctl(device->fd, CVORG_IOCSOUTGAIN, outgain);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Get the analog output gain of the current channel
 * @param device	- CVORG device handle
 * @param outgain	- Retrieved analog output gain, in dB
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_get_outgain(cvorg_t *device, int32_t *outgain)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	ret = ioctl(device->fd, CVORG_IOCGOUTGAIN, outgain);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Set the input polarity of the current channel
 * @param device	- CVORG device handle
 * @param inpol		- Desired polarity at the inputs
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_set_inpol(cvorg_t *device, enum cvorg_inpol inpol)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d inpol: %s\n", device->fd,
		inpol == CVORG_POSITIVE_PULSE ? "positive" : "negative");
	ret = ioctl(device->fd, CVORG_IOCSINPOLARITY, &inpol);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Get the input polarity of the current channel
 * @param device	- CVORG device handle
 * @param inpol		- Retrieved input polarity
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_get_inpol(cvorg_t *device, enum cvorg_inpol *inpol)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	ret = ioctl(device->fd, CVORG_IOCGINPOLARITY, inpol);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Get the status of the current channel
 * @param device	- CVORG device handle
 * @param status	- Retrieved status (in CVORG_CHANSTAT_ bitmask format)
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_get_status(cvorg_t *device, unsigned int *status)
{
	int ret;

	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	ret = ioctl(device->fd, CVORG_IOCGCHANSTAT, status);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Send software trigger to the device
 * @param device	- CVORG device handle
 * @param trigger	- trigger to be sent
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_set_trigger(cvorg_t *device, enum cvorg_trigger trigger)
{
	int ret;

	if (__cvorg_loglevel >= 4) {
		fprintf(stderr, "%s: fd %d ", __func__, device->fd);
		if (trigger == CVORG_TRIGGER_START)
			fprintf(stderr, "start\n");
		else if (trigger == CVORG_TRIGGER_STOP)
			fprintf(stderr, "stop\n");
		else if (trigger == CVORG_TRIGGER_EVSTOP)
			fprintf(stderr, "event stop\n");
		else
			fprintf(stderr, "unknown trigger\n");
	}
	ret = ioctl(device->fd, CVORG_IOCSTRIGGER, &trigger);

	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

int __cvorg_channel_set_sequence(cvorg_t *device, struct cvorg_seq *sequence)
{
	int ret;

	ret = ioctl(device->fd, CVORG_IOCSLOADSEQ, sequence);
	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Load a CVORG sequence onto the device
 * @param device	- CVORG device handle
 * @param sequence	- CVORG sequence to be loaded
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_set_sequence(cvorg_t *device, struct cvorg_seq *sequence)
{
	LIBCVORG_DEBUG(4, "fd %d nr: %d n_waves: %d waves: %p\n", device->fd,
		sequence->nr, sequence->n_waves, sequence->waves);
	return __cvorg_channel_set_sequence(device, sequence);
}

/**
 * @brief Load a CVORG waveform onto the device
 * @param device	- CVORG device handle
 * @param waveform	- CVORG waveform to be loaded
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_set_waveform(cvorg_t *device, struct cvorg_wv *waveform)
{
	struct cvorg_seq seq;

	LIBCVORG_DEBUG(4, "fd %d recurr %d size %d dynamic_gain %d gain_val %d "
		"form: %p\n", device->fd, waveform->recurr, waveform->size,
		waveform->dynamic_gain, waveform->gain_val, waveform->form);
	memset(&seq, 0, sizeof(seq));

	seq.nr		= 1;
	seq.n_waves	= 1;
	seq.waves	= waveform;

	return __cvorg_channel_set_sequence(device, &seq);
}

/**
 * @brief Retrieve the hardware revision of a CVORG device
 * @param device	- CVORG device handle
 *
 * @return a string with the version on success, NULL on failure
 */
char *cvorg_get_hw_version(cvorg_t *device)
{
	static SkelDrvrVersion vers;
	int ret;

	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	ret = ioctl(device->fd, SkelDrvrIoctlGET_VERSION, &vers);
	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return NULL;
	}
	return vers.ModuleVersion;
}

static int __cvorg_channel_enable_output(cvorg_t *device, int enable)
{
	int32_t val = enable;
	int ret;

	ret = ioctl(device->fd, CVORG_IOCSOUT_ENABLE, &val);
	if (ret < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

/**
 * @brief Disable the channel's output
 * @param device	- CVORG device handle
 *
 * @return 0 on success, -1 on failure
 *
 * This function does not disturb a sequence while it is being played.
 * In that case, the output will be disabled immediately after the completion
 * of the sequence.
 */
int cvorg_channel_disable_output(cvorg_t *device)
{
	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	return __cvorg_channel_enable_output(device, 0);
}

/**
 * @brief Enable the channel's output
 * @param device	- CVORG device handle
 *
 * @return 0 on success, -1 on failure
 */
int cvorg_channel_enable_output(cvorg_t *device)
{
	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	return __cvorg_channel_enable_output(device, 1);
}

static int dac_set_val(cvorg_t *device, int val)
{
	uint32_t value = val;

	if (ioctl(device->fd, CVORG_IOCSDAC_VAL, &value) < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

static int dac_set_gain(cvorg_t *device, int val)
{
	uint16_t value = (uint16_t)val;

	if (ioctl(device->fd, CVORG_IOCSDAC_GAIN, &value) < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}

static int dac_set_offset(cvorg_t *device, int val)
{
	int16_t value = (uint16_t)val;

	if (ioctl(device->fd, CVORG_IOCSDAC_OFFSET, &value) < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}
	return 0;
}


static int __dac(cvorg_t *device, struct cvorg_dac conf)
{
	int ret;

	ret = dac_set_val(device, conf.value);
	if (ret)
		return ret;

	ret = dac_set_gain(device, conf.gain);
	if (ret)
		return ret;

	ret = dac_set_offset(device, conf.offset);
	if (ret)
		return ret;

	return 0;

}

static int __cvorg_dac_set_conf(cvorg_t *device, struct cvorg_dac conf)
{	
	uint32_t mode;
	int ret;

	mode = CVORG_MODE_DAC;
	if (ioctl(device->fd, CVORG_IOCSMODE, &mode) < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}

	ret = __dac(device, conf);

	mode = CVORG_MODE_OFF;
	if (ioctl(device->fd, CVORG_IOCSMODE, &mode) < 0) {
		__cvorg_libc_error(__func__);
		return -1;
	}

	return ret;
}

/**
 * @brief Set a new configuration for the internal DAC.
 * @param device	- CVORG device handle
 * @param conf 		- Configuration
 *
 * @return 0 on success, -1 on failure
 *
 * This function allows calibrate the current channel's DAC of the device.
 */
int cvorg_dac_set_conf(cvorg_t *device, struct cvorg_dac conf)
{
	unsigned int status;
	int ret;
	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);

	/* Check if the current channel is busy or not */
	ret = cvorg_channel_get_status(device, &status);
	if(ret)
		return ret;

	if (status & CVORG_CHANSTAT_BUSY ||
		status & CVORG_CHANSTAT_SRAM_BUSY) {
		fprintf(stderr, "current channel is busy. Cannot set DAC configuration.\n");
		return -1; /* XXX SIG: this return value or another? */
	}

	/* Setup the configuration */
	return __cvorg_dac_set_conf(device, conf);
}

static int __cvorg_dac_get_conf(cvorg_t *device, struct cvorg_dac *conf)
{	
	uint32_t mode;
	uint32_t val;
	uint16_t gain;
	int16_t offset;

	mode = CVORG_MODE_DAC;
	if (ioctl(device->fd, CVORG_IOCSMODE, &mode) < 0)
		goto error;

	if (ioctl(device->fd, CVORG_IOCGDAC_VAL, &val) < 0)
		goto error;

	if (ioctl(device->fd, CVORG_IOCGDAC_GAIN, &gain) < 0)
		goto error;

	if (ioctl(device->fd, CVORG_IOCGDAC_OFFSET, &offset) < 0)
		goto error;

	mode = CVORG_MODE_OFF;
	if (ioctl(device->fd, CVORG_IOCSMODE, &mode) < 0)
		goto error;

	conf->value = val;
	conf->gain = gain;
	conf->offset = offset;

	return 0;
error:
	__cvorg_libc_error(__func__);
	return -1;
	
}

/**
 * @brief Retrieve the configuration for the internal DAC.
 * @param device	- CVORG device handle
 * @param conf 		- Configuration
 *
 * @return 0 on success, -1 on failure
 *
 * This function reads the configuration of the current channel's DAC of the device.
 */
int cvorg_dac_get_conf(cvorg_t *device, struct cvorg_dac *conf)
{
	LIBCVORG_DEBUG(4, "fd %d\n", device->fd);
	return __cvorg_dac_get_conf(device, conf);
}



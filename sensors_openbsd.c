/*
Copyright (c) 2016, Al Poole <netstar@gmail.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/sensors.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>

/* Variables for output */
static bool audio_output_simple = true;

/* Storage for sensor access (ioctl) */
static char *sensor_name = NULL;
size_t slen = sizeof(struct sensor);
struct sensor snsr;
struct sensordev snsrdev;
size_t sdlen = sizeof(struct sensordev);
int devn;

/* Storage for sensor values */
static int mib[5] = {CTL_HW, HW_SENSORS, 0, 0, 0};
static int *bat_mib[5] = { NULL };
static int pwr_mib[5];
static int temperature_mib[5];

#define MAX_BATTERIES 5
/* If there is > 1 battery just add the values */
static int battery_index = 0;
static double last_full_charge = 0;
static double current_charge = 0;

/* Structure to store output for printing to stdout */
typedef struct results_t results_t;
struct results_t {
    bool 	    have_power;
    uint8_t 	    battery_percent;
    bool 	    is_centigrade;
    uint8_t 	    temperature;
    bool 	    have_mixer;
    uint8_t 	    volume_left;
    uint8_t 	    volume_right;
};

static int 
openbsd_audio_state_master(results_t * results)
{
    int 	    i;
    mixer_devinfo_t dinfo;
    mixer_ctrl_t   *values = NULL;
    mixer_devinfo_t *info = NULL;

    int fd = open("/dev/mixer", O_RDONLY);
    if (fd < 0)
	return (0);

    for (devn = 0;; devn++) {
	dinfo.index = devn;
	if (ioctl(fd, AUDIO_MIXER_DEVINFO, &dinfo))
	    break;
    }

    info = calloc(devn, sizeof(*info));
    if (!info)
	return (0);

    for (i = 0; i < devn; i++) {
	info[i].index = i;
	if (ioctl(fd, AUDIO_MIXER_DEVINFO, &info[i]) == -1) {
	    --devn;
	    --i;
	    results->have_mixer = true;
	    continue;
	}
    }

    values = calloc(devn, sizeof(*values));
    if (!values)
	return (0);

    for (i = 0; i < devn; i++) {
	values[i].dev = i;
	values[i].type = info[i].type;
	if (info[i].type != AUDIO_MIXER_CLASS) {
	    values[i].un.value.num_channels = 2;
	    if (ioctl(fd, AUDIO_MIXER_READ, &values[i]) == -1) {
		values[i].un.value.num_channels = 1;
		if (ioctl(fd, AUDIO_MIXER_READ, &values[i]) == -1)
		    return (0);
	    }
	}
    }

    char name[64];

    for (i = 0; i < devn; i++) {
	strlcpy(name, info[i].label.name, sizeof(name));
	if (!strcmp("master", name)) {
	    results->volume_left = values[i].un.value.level[0];
	    results->volume_right = values[i].un.value.level[1];
	    results->have_mixer = true;
	    break;
	}
    }

    close(fd);

    if (values)
	free(values);
    if (info)
	free(info);

    return (results->have_mixer);
}

static void 
openbsd_temperature_state(results_t * results)
{
    memcpy(&temperature_mib, mib, sizeof(int) * 5);

    for (devn = 0;; devn++) {
	temperature_mib[2] = devn;

	if (sysctl(temperature_mib, 3, &snsrdev, &sdlen, NULL, 0) == -1) {
	    if (errno == ENOENT)
		break;
	    else
		continue;
	}
	if (!strcmp("cpu0", snsrdev.xname)) {
	    sensor_name = strdup("cpu0");
	    break;
	} else if (!strcmp("km0", snsrdev.xname)) {
	    sensor_name = strdup("km0");
	    break;
	}
    }

    int numt;

    for (numt = 0; numt < snsrdev.maxnumt[SENSOR_TEMP]; numt++) {
	temperature_mib[4] = numt;

	if (sysctl(temperature_mib, 5, &snsr, &slen, NULL, 0) == -1)
	    continue;

	if (slen > 0 && (snsr.flags & SENSOR_FINVALID) == 0)
	    break;
    }

    int temp = 0;

    if (sysctl(temperature_mib, 5, &snsr, &slen, NULL, 0) != -1) {
	temp = (snsr.value - 273150000) / 1000000.0;
    }
    results->temperature = temp;
}

/* just add the values for all batteries. */


static int 
openbsd_mibs_power_get(void)
{
    int i;
    int result = 0;    

    for (devn = 0;; devn++) {
	mib[2] = devn;
	if (sysctl(mib, 3, &snsrdev, &sdlen, NULL, 0) == -1) {
	    if (errno == ENXIO)
		continue;
	    if (errno == ENOENT)
		break;
	}

        for (i = 0; i < MAX_BATTERIES; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "acpibat%d", i);
       	    if (!strcmp(buf, snsrdev.xname)) {
                bat_mib[battery_index] = malloc(sizeof(int) * 5);
       	        int *tmp = bat_mib[battery_index++];
        	tmp[0] = mib[0];
        	tmp[1] = mib[1];
        	tmp[2] = mib[2];
       	    }
            result++;
        }

	if (!strcmp("acpiac0", snsrdev.xname)) {
	    pwr_mib[0] = mib[0];
	    pwr_mib[1] = mib[1];
	    pwr_mib[2] = mib[2];
	}
    }
 
    return (result);
}


static void
openbsd_battery_state_get(int *mib, results_t * results)
{
    mib[3] = 7;
    mib[4] = 0;

    if (sysctl(mib, 5, &snsr, &slen, NULL, 0) != -1)
	last_full_charge += (double) snsr.value;

    mib[3] = 7;
    mib[4] = 3;

    if (sysctl(mib, 5, &snsr, &slen, NULL, 0) != -1)
	current_charge += (double) snsr.value;

    /* There is a bug in the OS so try again... */
    if (current_charge == 0 || last_full_charge == 0) {
	mib[3] = 8;
	mib[4] = 0;

	if (sysctl(mib, 5, &snsr, &slen, NULL, 0) != -1)
	    last_full_charge += (double) snsr.value;

	mib[3] = 8;
	mib[4] = 3;

	if (sysctl(mib, 5, &snsr, &slen, NULL, 0) != -1)
	    current_charge += (double) snsr.value;
    }
}

static void openbsd_power_state(results_t *results)
{
    int i;
    int have_power = 0;

    pwr_mib[3] = 9;
    pwr_mib[4] = 0;

    if (sysctl(pwr_mib, 5, &snsr, &slen, NULL, 0) != -1)
	have_power = (int) snsr.value;

    // get batteries here
    for (i = 0; i < battery_index; i++) {
        openbsd_battery_state_get(bat_mib[i], results);
    }

    for (i = 0; i < battery_index; i++)
        free(bat_mib[i]);

    double percent = 100 * (current_charge / last_full_charge);

    results->battery_percent = (int) percent;
    results->have_power = have_power;
}

static int 
get_percent(int value, int max)
{
    double avg = (max / 100.0);
    double tmp = value / avg;

    int result = round(tmp);

    return (result);
}

static void 
results_show(results_t results)
{
    if (results.have_power)
	printf("[AC]: %d%%", results.battery_percent);
    else
	printf("[DC]: %d%%", results.battery_percent);

    printf(" [TEMP]: %dC", results.temperature);

    if (results.have_mixer) {
	if (audio_output_simple) {
	    uint8_t 	    high = results.volume_right > results.volume_left ?
	    results.volume_right : results.volume_left;
	    uint8_t 	    perc = get_percent(high, 255);
	    printf(" [AUDIO]: %d%%", perc);
	} else
	    printf(" [AUDIO] L: %d R: %d", results.volume_left,
		   results.volume_right);
    }
    printf("\n");
}

int 
main(int argc, char **argv)
{
    results_t results;

    memset(&results, 0, sizeof(results_t));

    bool have_battery = openbsd_mibs_power_get();
   
    if (have_battery) {
        openbsd_power_state(&results);
    }

    openbsd_temperature_state(&results);
    openbsd_audio_state_master(&results);

    results_show(results);
    
    return (EXIT_SUCCESS);
}


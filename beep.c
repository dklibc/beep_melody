/*
 * Make a single beep of user defined tone and duration on a beeper.
 *
 * Depends: Linux, input-evdev
 *
 * Copyright (C) 2021 Denis Kalashnikov <denis281089@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/input.h>

static void show_help(void) {
	static const char *help_str =
		"Make beep by sending input event to beeper.\n\n"
		"Usage: beep [OPTIONS]\n\n"
		"Options:\n"
		"* -f HZ -- beep frequency (tone) in HZ. Default is 800,\n"
		"* -d ms -- duration in milliseconds. Default is 200ms,\n"
		"* -e N -- input event number (/dev/input/eventN). Default is 0,\n"
		"* -h -- show this help,\n";

	fprintf(stderr, "%s\n", help_str);
}

static int send_sound_event(int fd, int code, int val)
{
	static struct input_event ev;

	ev.type = EV_SND;
	ev.code = code;
	ev.value = val;

	return write(fd, &ev, sizeof(ev)) != sizeof(ev);
}

int main(int argc, char *argv[])
{
	int c, fd;
	int snd_code = SND_BELL;
	int freq = 1, duration_ms = 200, event_num = 0;
	char event_dev[256];

	while ((c = getopt(argc, argv, "d:f:e:h")) != -1) {
		switch(c) {
		case 'd':
			duration_ms = atoi(optarg);
			break;
		case 'f':
			freq = atoi(optarg);
			snd_code = SND_TONE;
			break;
		case 'e':
			event_num = atoi(optarg);
			break;
		case 'h':
			show_help();
			return 0;
		case '?':
		default:
			fprintf(stderr, "Invalid option: '%c'. Use '-h' for help\n", optopt);
			return 1;
		}
	}

	if (optind != argc) {
		fprintf(stderr, "Unexpected argument. Use '-h' for help\n");
		return 1;
	}

	snprintf(event_dev, sizeof(event_dev), "/dev/input/event%d", event_num);
	if ((fd = open(event_dev, O_WRONLY)) < 0) {
		fprintf(stderr, "Failed to open event device \"%s\": %s\n",
			event_dev, strerror(errno));
		return 1;
	}

	send_sound_event(fd, snd_code, freq);
	usleep(duration_ms * 1000);
	send_sound_event(fd, snd_code, 0);

	close(fd);
	return 0;
}

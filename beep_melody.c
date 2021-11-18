/*
 * Based on code from https://dragaosemchama.com/en/2019/02/songs-for-arduino/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/input.h>

#include "beep_melody.h"

/*
 * Tetris theme - (Korobeiniki)
 * Notes of the melody followed by the duration.
 * 4 means a quarter note, 8 an eighteenth , 16 sixteenth, so on,
 * !!negative numbers are used to represent dotted notes,
 */
static int melody[] = {
	/* Based on the arrangement at https://www.flutetunes.com/tunes.php?id=192 */
	NOTE_E5,4,  NOTE_B4,8,  NOTE_C5,8,  NOTE_D5,4,  NOTE_C5,8,  NOTE_B4,8,
	NOTE_A4,4,  NOTE_A4,8,  NOTE_C5,8,  NOTE_E5,4,  NOTE_D5,8,  NOTE_C5,8,
	NOTE_B4,-4,  NOTE_C5,8,  NOTE_D5,4,  NOTE_E5,4,
	NOTE_C5,4,  NOTE_A4,4,  NOTE_A4,8,  NOTE_A4,4,  NOTE_B4,8,  NOTE_C5,8,

	NOTE_D5,-4,  NOTE_F5,8,  NOTE_A5,4,  NOTE_G5,8,  NOTE_F5,8,
	NOTE_E5,-4,  NOTE_C5,8,  NOTE_E5,4,  NOTE_D5,8,  NOTE_C5,8,
	NOTE_B4,4,  NOTE_B4,8,  NOTE_C5,8,  NOTE_D5,4,  NOTE_E5,4,
	NOTE_C5,4,  NOTE_A4,4,  NOTE_A4,4, REST, 4,

	NOTE_E5,4,  NOTE_B4,8,  NOTE_C5,8,  NOTE_D5,4,  NOTE_C5,8,  NOTE_B4,8,
	NOTE_A4,4,  NOTE_A4,8,  NOTE_C5,8,  NOTE_E5,4,  NOTE_D5,8,  NOTE_C5,8,
	NOTE_B4,-4,  NOTE_C5,8,  NOTE_D5,4,  NOTE_E5,4,
	NOTE_C5,4,  NOTE_A4,4,  NOTE_A4,8,  NOTE_A4,4,  NOTE_B4,8,  NOTE_C5,8,

	NOTE_D5,-4,  NOTE_F5,8,  NOTE_A5,4,  NOTE_G5,8,  NOTE_F5,8,
	NOTE_E5,-4,  NOTE_C5,8,  NOTE_E5,4,  NOTE_D5,8,  NOTE_C5,8,
	NOTE_B4,4,  NOTE_B4,8,  NOTE_C5,8,  NOTE_D5,4,  NOTE_E5,4,
	NOTE_C5,4,  NOTE_A4,4,  NOTE_A4,4, REST, 4,

	NOTE_E5,2,  NOTE_C5,2,
	NOTE_D5,2,   NOTE_B4,2,
	NOTE_C5,2,   NOTE_A4,2,
	NOTE_GS4,2,  NOTE_B4,4,  REST,8,
	NOTE_E5,2,   NOTE_C5,2,
	NOTE_D5,2,   NOTE_B4,2,
	NOTE_C5,4,   NOTE_E5,4,  NOTE_A5,2,
	NOTE_GS5,2,

	MELODY_END,
};

static void beeper_tone(int fd, int freq, int duration_usec)
{
	static struct input_event ev;

	ev.type = EV_SND;
	ev.code = SND_TONE;
	ev.value = freq;
	write(fd, &ev, sizeof(ev));
	usleep(duration_usec);
	ev.value = 0;
	write(fd, &ev, sizeof(ev));
}

static void play_melody(int fd, int melody[], int tempo)
{
	int whole_note_ms = (60000 * 4) / tempo;
	int divider = 0, note_duration_ms = 0;
	int i;

	for (i = 0; melody[i] != MELODY_END; i = i + 2) {
		/* Calculate the duration of each note */
		divider = melody[i + 1];
		if (divider > 0) { /* Regular note */
			note_duration_ms = whole_note_ms / divider;
		} else if (divider < 0) { /* Dotted note */
			divider = -divider;
			note_duration_ms = whole_note_ms / divider;
			note_duration_ms += note_duration_ms / 2;
		}

		/* We only play the note for 90% of the duration, leaving 10% as a pause */
		beeper_tone(fd, melody[i], note_duration_ms * 900);
		usleep(note_duration_ms * 100);
	}
}

static void show_help(void) {
	static const char *help_str =
		"Play melody on beeper.\n\n"
		"Usage: beep_melody [OPTIONS]\n\n"
		"Options:\n"
		"* -e N -- input event number (/dev/input/eventN). Default is 0,\n"
		"* -h -- show this help,\n";

	fprintf(stderr, "%s\n", help_str);
}

int main(int argc, char *argv[])
{
	int fd, c, event_num = 0;
	char event_dev[256];

	while ((c = getopt(argc, argv, "e:h")) != -1) {
		switch(c) {
		case 'e':
			event_num = atoi(optarg);
			break;
		case 'h':
			show_help();
			return 0;
		case '?':
		default:
			fprintf(stderr, "Invalid option: '%c'. Use '-h' for help\n", optopt);
			return -1;
		}
	}

	if (optind != argc) {
		fprintf(stderr, "Unexpected argument. Use '-h' for help\n");
		return -1;
	}

	snprintf(event_dev, sizeof(event_dev), "/dev/input/event%d", event_num);
	if ((fd = open(event_dev, O_WRONLY)) < 0) {
		fprintf(stderr, "Failed to open event device \"%s\": %s\n",
			event_dev, strerror(errno));
		return -1;
	}

	play_melody(fd, melody, 144);

	close(fd);
	return 0;
}

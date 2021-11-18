/*
 * Play a melody written on the Nokia ringtone language
 * (https://en.wikipedia.org/wiki/Ring_Tone_Text_Transfer_Language)
 * on a beeper/piezo-buzzer. Buzzer must support of changing
 * tone and beep duration.
 *
 * Depends: Linux, input-evdev.
 *
 * Usage example: ./beep_melody <<<"TheLambada:d=8,o=5,b=125:4d.6,c6,a#,a,4g,g,a#,a,g,f,g,d,c,2d.,4d.6,c6,a#,a,4g,g,a#,a,g,f,g,d.,c,2d,c6,c6,c6,a#,4d#,d#,g,d.6,c.6,a#,4d#,g,a#,4a,g,f,4f,g,f,2g"
 *
 * Copyright (C) 2021 Denis Kalashnikov <denis281089@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

#include <linux/input.h>

/* Default values */
static int octave;   /* Possible values: 4-7 */
static int duration; /* Possible values: 1, 2, 4, 8, 16, 32 */
static int tempo;    /* Possible values: 40-200 */
static int whole_note_ms;

static int debug;

enum log_levels {
	LOG_DBG=0,
	LOG_INFO=1,
	LOG_WARN=2,
	LOG_ERR=9,
};

#define DEBUG(frmt, ...) _log(LOG_DBG, frmt, ##__VA_ARGS__)
#define WARN(frmt, ...)  _log(LOG_WARN, frmt, ##__VA_ARGS__)
#define ERR(frmt, ...)   _log(LOG_ERR, frmt, ##__VA_ARGS__)

static void _log(int level, const char *frmt, ...)
{
	static const char *l[] = {
		[LOG_DBG] = "DEBUG",
		[LOG_INFO] = "INFO",
		[LOG_WARN] = "WARNING",
		[LOG_ERR] = "ERROR",
	};
	va_list args;

	if (!debug && level == LOG_DBG)
		return;

	va_start(args, frmt);
	fprintf(stderr, "%s: ", l[level]);
	vfprintf(stderr, frmt, args);
	fprintf(stderr, "\n");
	va_end(args);
}

/*
 * In: @ni -- note index (for error messages), @s -- note string
 *   in format: "[<duration>][CDEFGABP][#][.][<octave>]",
 * Out: @freq, @duration_usec,
 * Return: 0 -- Ok, <0 -- error,
 */
static int parse_note(int ni, const char *s, int *freq, int *duration_usec)
{
	const char *p = s;
	int c, note, sharp = 0, dot = 0, cur_duration, cur_octave;
	static short tone[4][12] = {
		/* C, C#, D, D#, E, F, F#, G, G#, A, A#, B */
		{ 262, 277, 294, 311, 330, 349, 370, 392, 415,
		  440, 466, 494 }, /* Octave #4 */
		{ 523, 554, 587, 622, 659, 698, 740, 784, 831,
		  880, 932, 988 }, /* Octave #5 */
		{ 1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568,
		  1661, 1760, 1865, 1976 }, /* Octave #6 */
		{ 2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136,
		  3322, 3520, 3729, 3951 }, /* Octave #7 */
	};
	/* Note column in tone array */
	static signed char note_tone_col[2][7] = {
		/* A, B, C, D, E, F, G */
		{ 9, 11, 0, 2, 4, 5, 7 }, /* sharp=0 */
		{ 10, 11, 1, 3, 4, 6, 8 }, /* sharp=1 */
	};

	/* Get duration */
	c = *p++;
	switch (c) {
	case '1':
		if (*p == '6') {
			p++;
			cur_duration = 16;
			break;
		}
	case '2':
	case '4':
	case '8':
		cur_duration = c - '0';
		break;
	case '3':
		if (*p++ != '2') {
			WARN("Note #%d: expected duration 32", ni);
			return -1;
		}
		cur_duration = 32;
		break;
	default:
		cur_duration = duration;
		p--;
		break;
	}

	*duration_usec = whole_note_ms * 1000 / cur_duration;

	/* Get note */
	c = *p++;
	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	if (c >= 'A' && c <= 'G' || c == 'P' /* Pause */) {
		note = c;
	} else {
		WARN("Note #%d: expected note (CDEFGAB)", ni);
		return -1;
	}

	c = *p;
	if (c == '#') {
		sharp = 1;
		c = *++p;
		if (c == '.') {
			dot = 1;
			p++;
		}
	} else if (c == '.') {
		dot = 1;
		p++;
	}

	if (dot) {
		*duration_usec += *duration_usec/2;
	}

	/* Get octave */
	c = *p;
	if (!c) {
		cur_octave = octave;
	} else {
		if (c < '4' || c > '7') {
			WARN("Note #%d: expected octave (4-7)", ni);
			return -1;
		}
		cur_octave = c - '0';
	}


	*freq = (note == 'P') ? 0 :
		tone[cur_octave - 4][note_tone_col[sharp][note - 'A']];

	DEBUG("Note #%d: note = %c%c, octave = %d, duration = %d%c, freq,HZ = %d,"
		" duration,msecs = %d", ni, note, sharp ? '#' : ' ', cur_octave,
		cur_duration, dot ? '.' : ' ', *freq, *duration_usec/1000);

	return 0;
}

static inline const char *skip_ws(const char *s)
{
	while (isspace(*s))
		s++;
	return s;
}

/*
 * Parse numeric parameter setting in format: "letter=num".
 * After num expects ',' or '\0'. Used for parsing melody
 * defaults section (e.g. "o=5,b=120,d=4").
 *
 * Return: pointer to the next char or NULL (in case of error
 *   or empty string).
 * Out: @c -- parameter letter, @n -- parameter numeric
 *   value (max 999), @err -- error status.
 */
static const char *char_eq_num(const char *s, int *c, int *n, int *err)
{
	*err = 0;
	s = skip_ws(s);
	if (!*s)
		return NULL;
	*err = -1;
	*c = *s++;
	s = skip_ws(s);
	if (*s != '=')
		return NULL;
	s = skip_ws(s + 1);
	if (!isdigit(*s))
		return NULL;
	*n = *s++ - '0';
	while (isdigit(*s) && *n < 999) {
		*n =  *n * 10 + (*s - '0');
		s++;
	}
	if (*s) {
		if (*s++ != ',')
			return NULL;
	}
	*err = 0;
	return s;
}

/*
 * Parse string of numeric parameters separated with comma.
 * Used for parsing melody defaults section, e.g. "d=4,o=5,b=120".
 *
 * Out: @num[26]: <0 -- missing, >=0 -- parameter value.
 * Return: 0 -- Ok, <0 -- error.
 */
static int parse_char_eq_num_str(const char *s, int num[26])
{
	int i, c, n, err;

	for (i = 0; i < 26; i++)
		num[i] = -1;
	while (s = char_eq_num(s, &c, &n, &err)) {
		if (num[c - 'a'] < 0) {
			num[c - 'a'] = n;
		} else {
			WARN("Default param '%c' has been already set", c);
		}
	}
	return err;
}

#define DEFAULTS(c) (defaults[(c) - 'a'])

/*
 * Set defaults from melody defaults section.
 *
 * Return: 0 -- Ok, <0 -- error (invalid or missing defaults).
 */
static int set_defaults(const int defaults[26])
{
	int n;

	n = DEFAULTS('o');
	if (n < 0) {
		ERR("Missing required default octave");
		return -1;
	}
	if (n < 4 || n > 7) {
		ERR("Invalid default octave, must be 4-7");
		return -1;
	}
	octave = n;

	n = DEFAULTS('d');
	if (n < 0) {
		ERR("Missing required default duration");
		return -1;
	}
	if (n != 1 && n != 2 && n != 4 && n != 8 && n != 16 && n != 32) {
		ERR("Invalid default duration, must be 1,2,4,8,16,32");
		return -1;
	}
	duration = n;

	n = DEFAULTS('b');
	if (n < 0) {
		ERR("Missing required default beats");
		return -1;
	}
	if (n < 40 || n > 200) {
		ERR("Invalid default beats, must be 40-200");
		return -1;
	}
	tempo = n;

	DEBUG("Defaults: octave=%d, duration=%d, beats/tempo=%d", octave, duration, tempo);

	whole_note_ms = (60000 * 4) / tempo;
	DEBUG("Note duration,ms: %d", whole_note_ms);

	return 0;
}

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

/* Play melody */
static int play(int fd, const char *melody)
{
	const char *p, *q;
	int freq, duration_usec;
	char buf[32];
	int defaults[26];
	int n, i;

	/* Skip melody name (if there) */
	p = strchr(melody, ':');
	if (p)
		melody = p++;

	/* Parse defaults section */
	q = strchr(p, ':');
	if (!p) {
		ERR("Missing required defaults section in melody");
		return -1;
	}

	if (q - p >= sizeof(buf)) {
		ERR("Too long defaults section");
		return -1;
	}
	strncpy(buf, p, q - p);
	buf[q - p] = '\0';
	p = ++q;

	DEBUG("Defaults section: %s", buf);

	if (parse_char_eq_num_str(buf, defaults)) {
		ERR("Failed to parse defaults section");
		return -1;
	}

	if (set_defaults(defaults))
		return -1;

	/* Parse notes section and play */
	for (i = 1, n = strlen(melody); (p < melody + n)
	  && (p = skip_ws(p)); i++) {
		q = strchr(p, ',');
		if (!q)
			q = melody + n;
		if (q - p >= sizeof(buf)) {
			ERR("Too long note #%d", i);
			return -1;
		}
		strncpy(buf, p, q - p);
		buf[q - p] = '\0';
		DEBUG("Note #%d: %s", i, buf);
		p = ++q;
		if (parse_note(i, buf, &freq, &duration_usec))
			continue;
		beeper_tone(fd, freq, duration_usec);
		usleep(duration_usec / 4);
	}

	return 0;
}

static void show_help(void) {
	static const char *help_str =
		"Play melody on beeper.\n\n"
		"Usage: beep_melody [OPTIONS]\n\n"
		"Options:\n"
		"* -e N -- input event number (/dev/input/eventN). Default is 0,\n"
		"* -d -- debug,\n"
		"* -h -- show this help,\n";

	fprintf(stderr, "%s\n", help_str);
}

int main(int argc, char *argv[])
{
	int fd, event_num = 0, c, n;
	char event_dev[256];
	char melody[1024];

	while ((c = getopt(argc, argv, "e:dh")) != -1) {
		switch(c) {
		case 'e':
			event_num = atoi(optarg);
			break;
		case 'd':
			debug = 1;
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

	if (fgets(melody, sizeof(melody) - 1, stdin)) {
		n = strlen(melody);
		if (!n)
			return 0;
		if (melody[n - 1] == '\n')
			melody[n - 1] = '\0';
	} else {
		ERR("Failed to read melody from stdin: %s",
		  strerror(errno));
		return -1;
	}

	snprintf(event_dev, sizeof(event_dev), "/dev/input/event%d", event_num);
	if ((fd = open(event_dev, O_WRONLY)) < 0) {
		ERR("Failed to open event device \"%s\": %s\n",
		  event_dev, strerror(errno));
		return -1;
	}

	return play(fd, melody);
}

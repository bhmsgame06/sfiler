#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <termios.h>
#include "../serial.h"

static char *program_name;

static int interrupt = 0;

/* default serial device */
static char *serial_device = "/dev/ttyACM0";
/* file descriptors */
static int serial_fd;
/* default current working directory */
static char *cwd = "/Other files";

static const struct option longopts[] = {
	{"help",                0, NULL, 'h'},
	{"chdir",               1, NULL, 'c'},
	{"device",              1, NULL, 'd'},
	{NULL, 0, NULL, 0}
};

static void preexit();
static int die(char *fmt, ...);
static void show_help(int err);
static int read_result_line();

/* deallocate resources */
static void preexit() {
	close(serial_fd);
}

/* deallocate resources and exit with error */
static int die(char *fmt, ...) {
	preexit();

	va_list v;
	va_start(v, fmt);
	vfprintf(stderr, fmt, v);
	va_end(v);

	exit(1);
}

/* print help to the terminal */
static void show_help(int err) {
	fprintf(err == 1 ? stderr : stdout,
			"Unlink the file.\n" \
			"\n" \
			"Usage: %s [options] <file> ...\n" \
			"\n" \
			"Available options:\n" \
			"  -h, --help                - print help and exit.\n" \
			"  -c, --chdir=<dir>         - change directory before transferring\n" \
			"                              (default: \"/Other files\").\n" \
			"  -d, --device=<file>       - serial device to operate on.\n",
			program_name);
}

/* read non-blank line */
static int read_result_line() {
	do {
		serial_response_read(serial_fd);
	} while(response_status == RESPONSE_UNKNOWN);

	return response_status;
}

/* main function */
int main(int argc, char *argv[]) {
	if(argv[0] == NULL)
		program_name = "srm";
	else
		program_name = argv[0];

	int c;
	while((c = getopt_long(argc, argv, "hc:d:", longopts, NULL)) != -1) {
		
		switch(c) {
			/* --help */
			case 'h':
				show_help(0);
				return 0;

			/* --chdir */
			case 'c':
				cwd = strdup(optarg);
				break;

			/* --device */
			case 'd':
				serial_device = strdup(optarg);
				break;

			default:
				show_help(1);
				return 1;
		}

	}

	argv += optind;
	argc -= optind;

	if(argc < 1) {
		show_help(1);
		return 1;
	}

	/* open serial device */
	serial_fd = open(serial_device, O_RDWR | O_NOCTTY);
	if(serial_fd < 0) {
		perror(serial_device);
		return errno;
	}
	struct termios tty;
	tcgetattr(serial_fd, &tty);
	tty.c_lflag &= ~(ISIG | ICANON | XCASE | IEXTEN | ECHO | ECHOK | ECHOKE | ECHOCTL);
	tty.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP | INLCR | IGNCR | ICRNL | IUCLC | IXON | IXANY | IXOFF | IMAXBEL);
	tty.c_oflag &= ~(OPOST);
	tcsetattr(serial_fd, TCSANOW, &tty);

	/* chdir command */
	serial_printf(serial_fd, "AT+FSCD=\"%s\"\r\n", cwd);
	if(read_result_line() != RESPONSE_STATUS_OK) {
		if(response_status == RESPONSE_RESULT)
			die("FSCD: %s:%s\n", response_buffer, serial_read_line(serial_fd));
		else
			die("FSCD: %s\n", response_buffer);
	}

	/* file read command */
	serial_printf(serial_fd, "AT+FSFE=-1,\"%s\"\r\n", argv[0]);
	if(read_result_line() != RESPONSE_STATUS_OK) {
		if(response_status == RESPONSE_RESULT)
			die("FSFE: %s:%s\n", response_buffer, serial_read_line(serial_fd));
		else
			die("FSFE: %s\n", response_buffer);
	}

	preexit();

	return 0;
}

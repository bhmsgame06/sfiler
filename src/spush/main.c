#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>
#include <termios.h>
#include <zlib.h>
#include "../serial.h"
#include "../progress.h"

#define BLOCK_LENGTH 501

static char *program_name;

static int interrupt = 0;

/* default serial device */
static char *serial_device = "/dev/ttyACM0";
/* file descriptors */
static int serial_fd;
static int file_fd;
/* default current working directory */
static char *cwd = "/Other files";
/* clicks to hard exit */
static int signals_to_interrupt;
/* microsecond delay between block transfer */
static long blk_delay;
/* progress thread */
static int progress = 1;
static pthread_t progress_thread;
static struct progress progress_ctx;

static const struct option longopts[] = {
	{"help",                0, NULL, 'h'},
	{"remove-restrictions", 0, NULL, 'R'},
	{"hide-progress",       0, NULL, 'P'},
	{"delay",               1, NULL, 'b'},
	{"chdir",               1, NULL, 'c'},
	{"device",              1, NULL, 'd'},
	{NULL, 0, NULL, 0}
};

static void preexit();
static int die(char *fmt, ...);
static void show_help(int err);
static char *filename_p(char *filename);
static void sig_handler(int sig);
static int read_result_line();
static void end_command();

/* deallocate resources */
static void preexit() {
	close(serial_fd);
	close(file_fd);

	if(progress) {
		progress_track_stop(&progress_ctx);
		printf("\n");
	}
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
			"Write a file to the device.\n" \
			"\n" \
			"Usage: %s [options] <src file> [dest file] ...\n" \
			"\n" \
			"Available options:\n" \
			"  -h, --help                - print help and exit.\n" \
			"  -R, --remove-restrictions - ignore restrictions. May cause phone crash.\n" \
			"  -P, --hide-progress       - hide estimated transmission speed and percentage.\n" \
			"  -b, --delay=<ms>          - microsecond delay between each block transfer.\n" \
			"                              Use it when the phone's terminal hangs up on\n" \
			"                              high-speed data transfer.\n" \
			"  -c, --chdir=<dir>         - change directory before transferring\n" \
			"                              (default: \"/Other files\").\n" \
			"  -d, --device=<file>       - serial device to operate on.\n",
			program_name);
}

/* return pointer to filename (from absolute path) */
static char *filename_p(char *filename) {
	for(char *fn = filename + strlen(filename); fn >= filename; fn--) {
		if(*fn == '/')
			return fn + 1;
	}

	return filename;
}

/* signal handler */
static void sig_handler(int sig) {
	if(++signals_to_interrupt >= 3) {
		die("Received 3 signals, exiting.");
	}
	interrupt = 1;
}

/* read line */
static int read_result_line() {
	while(serial_response_read(serial_fd) == RESPONSE_UNKNOWN) {
	}

	return response_status;
}

/* send end command */
static void end_command() {
	serial_printf(serial_fd, "#END>\r\n");
	while(read_result_line() != RESPONSE_UNKNOWN) {
	}
}

/* main function */
int main(int argc, char *argv[]) {
	if(argv[0] == NULL)
		program_name = "spush";
	else
		program_name = argv[0];

	int rst = 1;

	int c;
	while((c = getopt_long(argc, argv, "hRPb:c:d:", longopts, NULL)) != -1) {
		
		switch(c) {
			case 'h':
				show_help(0);
				return 0;

			case 'R':
				rst = 0;
				break;

			case 'P':
				progress = 0;
				break;

			case 'b':
				blk_delay = atol(optarg);
				break;

			case 'c':
				cwd = strdup(optarg);
				break;

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

	/* open source file */
	file_fd = open(argv[0], O_RDONLY);
	if(file_fd < 0) {
		close(serial_fd);
		perror(argv[0]);
		return errno;
	}

	int file_length;

	struct stat stat_buf;
	stat(argv[0], &stat_buf);
	if((file_length = stat_buf.st_size) == 0) {
		die("%s: Cannot send, file size == 0\n", argv[0]);
	}

	/* open serial port */
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
	tty.c_cc[CMIN] = 1;
	tty.c_cc[CTIME] = 0;
	tcsetattr(serial_fd, TCSANOW, &tty);

	char *dest_filename = argc < 2 ? filename_p(argv[0]) : argv[1];

	/* chdir command */
	serial_printf(serial_fd, "AT+FSCD=\"%s\"\r\n", cwd);
	while(read_result_line() != RESPONSE_STATUS_OK) {
		if(response_status == RESPONSE_RESULT)
			die("FSCD: %s:%s\n", response_buffer, serial_read_line(serial_fd));
		else
			die("FSCD: %s\n", response_buffer);
	}

	/* file erase command */
	serial_printf(serial_fd, "AT+FSFE=-1,\"%s\"\r\n", dest_filename);
	while(read_result_line() != RESPONSE_STATUS_OK) {
		if(response_status == RESPONSE_RESULT)
			die("FSFE: %s:%s\n", response_buffer, serial_read_line(serial_fd));
		else
			die("FSFE: %s\n", response_buffer);
	}

	/* file write command */
	serial_printf(serial_fd, "AT+FSFW=-1,\"%s\",0,\"\",%d\r\n", dest_filename, file_length);

	/* starting progress thread */
	if(progress) {
		progress_ctx.filename = argv[0];
		progress_ctx.file_length = file_length;
		progress_ctx.total_transmitted = 0;
		pthread_create(&progress_thread, NULL, (void *)progress_track_start, &progress_ctx);
	}

	char buffer[BLOCK_LENGTH];
	int total_transmitted = 0;

	/* writing blocks */
	for(int block_num = 0;
			total_transmitted < file_length && !interrupt;
			block_num++) {

		serial_response_read(serial_fd);

		if(response_status == RESPONSE_UNKNOWN) {
			if(!strcmp(response_buffer, "##>\r")) {
				int n_read;
				total_transmitted += (n_read = read(file_fd, buffer, BLOCK_LENGTH));
				serial_printf(serial_fd, "%u,", crc32(0, buffer, n_read));
				write(serial_fd, buffer, n_read);
				if(progress) progress_ctx.total_transmitted = total_transmitted;
			} else if(!strcmp(response_buffer, "#END>\r")) {
				break;
			}
		} else {
			if(response_status == RESPONSE_RESULT)
				die("Block %d: %s:%s\n", block_num, response_buffer, serial_read_line(serial_fd));
			else
				die("Block %d: %s\n", block_num, response_buffer);
		}

		if(blk_delay > 0) usleep(blk_delay);

	}

	if(interrupt) {
		end_command();
		die("Interrupted\n");
	}

	/* final */
	while(read_result_line() != RESPONSE_STATUS_OK) {
		if(response_status == RESPONSE_RESULT)
			die("Final: %s:%s\n", response_buffer, serial_read_line(serial_fd));
		else
			die("Final: %s\n", response_buffer);
	}

	preexit();

	return 0;
}

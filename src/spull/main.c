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
/* CRC32 check */
static int crc32_check = 1;
/* progress thread */
static int progress = 1;
static pthread_t progress_thread;
static struct progress progress_ctx;

static const struct option longopts[] = {
	{"help",                0, NULL, 'h'},
	{"hide-progress",       0, NULL, 'P'},
	{"disable-crc",         0, NULL, 'C'},
	{"delay",               1, NULL, 'b'},
	{"chdir",               1, NULL, 'c'},
	{"device",              1, NULL, 'd'},
	{NULL, 0, NULL, 0}
};

static void preexit();
static int die(char *fmt, ...);
static void sig_handler(int sig);
static void show_help(int err);
static char *filename_p(char *filename);
static void sig_handler(int sig);
static int read_result_line();
static void next_command();
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

/* signal handler */
static void sig_handler(int sig) {
	if(++signals_to_interrupt >= 3) {
		die("Received 3 signals, exiting.");
	}
	interrupt = 1;
}

/* print help to the terminal */
static void show_help(int err) {
	fprintf(err == 1 ? stderr : stdout,
			"Read a file from the device.\n" \
			"\n" \
			"Usage: %s [options] <src file> [dest file] ...\n" \
			"\n" \
			"Available options:\n" \
			"  -h, --help                - print help and exit.\n" \
			"  -P, --hide-progress       - hide estimated transmission speed and percentage.\n" \
			"  -C, --disable-crc         - disable CRC32 checking, may increase speed, but\n" \
			"                              output file may be corrupted.\n" \
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

/* read result line */
static int read_result_line() {
	while(serial_response_read(serial_fd) == RESPONSE_UNKNOWN) {
	}

	return response_status;
}

/* send next command */
static void next_command() {
	serial_printf(serial_fd, "##>\r\n");
}

/* send end command */
static void end_command() {
	serial_printf(serial_fd, "#END>\r\n");
	while(read_result_line() != RESPONSE_STATUS_OK) {
	}
}

/* main function */
int main(int argc, char *argv[]) {
	/* checking zeroth arg */
	if(argv[0] == NULL)
		program_name = "spull";
	else
		program_name = argv[0];

	/* parsing arguments */
	int c;
	while((c = getopt_long(argc, argv, "hPCb:c:d:", longopts, NULL)) != -1) {
		
		switch(c) {
			/* --help */
			case 'h':
				show_help(0);
				return 0;

			/* --hide-progress */
			case 'P':
				progress = 0;
				break;

			/* --disable-crc */
			case 'C':
				crc32_check = 0;
				break;

			/* --delay */
			case 'b':
				blk_delay = atol(optarg);
				break;

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

	/* the file on your hard drive */
	char *dest_filename = argc < 2 ? filename_p(argv[0]) : argv[1];

	/* length of pulled file */
	int file_length = 0;

	/* open serial device */
	serial_fd = open(serial_device, O_RDWR | O_NOCTTY);
	if(serial_fd < 0) {
		perror(serial_device);
		return errno;
	}
	/* setting up tty */
	struct termios tty;
	tcgetattr(serial_fd, &tty);
	tty.c_lflag &= ~(ISIG | ICANON | XCASE | IEXTEN | ECHO | ECHOK | ECHOKE | ECHOCTL);
	tty.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP | INLCR | IGNCR | ICRNL | IUCLC | IXON | IXANY | IXOFF | IMAXBEL);
	tty.c_oflag &= ~(OPOST);
	tty.c_cc[CMIN] = 1;
	tty.c_cc[CTIME] = 0;
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
	serial_printf(serial_fd, "AT+FSFR=-1,\"%s\"\r\n", argv[0]);

	/* checking response */
	while(1) {
		if(read_result_line() == RESPONSE_RESULT) {

			if(!strcmp(response_buffer, "FSFR"))
				break;
			else
				die("FSFR: %s:%s\n", response_buffer, serial_read_line(serial_fd));

		} else {
			die("FSFR: %s\n", response_buffer);
		}
	}

	/* parsing FSFR arguments */
	for(int last = 0, argn = 0; last == 0; argn++) {
		last = serial_response_read_arg(serial_fd);
		if(argn == 5) {
			file_length = atoi(response_buffer);
		}
	}

	/* setting signal handlers */
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	/* open destination file */
	file_fd = open(dest_filename, O_WRONLY | O_CREAT | O_TRUNC);
	if(file_fd < 0) {
		close(serial_fd);
		perror(dest_filename);
		return errno;
	}
	fchmod(file_fd, 0644);

	char buffer[512];
	int total_transmitted = 0;

	// starting progress thread
	if(progress) {
		progress_ctx.filename = argv[0];
		progress_ctx.file_length = file_length;
		progress_ctx.total_transmitted = 0;
		pthread_create(&progress_thread, NULL, (void *)progress_track_start, &progress_ctx);
	}
	
	// reading blocks
	for(int block_length, block_num = 0, crc32_checksum;
		   total_transmitted < file_length && !interrupt;
		   block_num++) {

		/* sending a command to get the next block of file */
		next_command();

		/* checking response */
		while(1) {
			if(read_result_line() == RESPONSE_RESULT) {

				if(!strcmp(response_buffer, "FSFR"))
					break;
				else
					die("FSFR block %d: %s:%s\n", block_num, response_buffer, serial_read_line(serial_fd));

			} else {
				die("FSFR block %d: %s\n", block_num, response_buffer);
			}
		}

		/* parsing FSFR arguments */
		for(int last = 0, argn = 0;
				argn < 3 && last == 0;
				argn++) {

			last = serial_response_read_arg(serial_fd);
			switch(argn) {
				case 0:
					block_length = atoi(response_buffer);
					break;

				case 2:
					if(crc32_check) crc32_checksum = atoi(response_buffer);
					break;
			}

		}

		int read_bytes = 0;

		/* reading bytes */
		do {
			read_bytes += read(serial_fd, buffer + read_bytes, block_length - read_bytes);
		} while(read_bytes < block_length);

		/* checking CRC32 */
		if(crc32_check && (int32_t)crc32_checksum != (int32_t)crc32(0, buffer, block_length)) {
			end_command();
			die("Bad CRC32! Block: %d\n", block_num);
		}

		/* writing buffer to file */
		write(file_fd, buffer, read_bytes);

		/* total transmitted */
		total_transmitted += read_bytes;
		if(progress) progress_ctx.total_transmitted = total_transmitted;

		/* sleep */
		if(blk_delay > 0) usleep(blk_delay);

	}

	/* final */
	if(interrupt) {
		end_command();
		die("Interrupted\n");
	} else {
		next_command();
	}

	while(read_result_line() != RESPONSE_STATUS_OK) {
		if(response_status == RESPONSE_RESULT)
			die("Final: %s:%s\n", response_buffer, serial_read_line(serial_fd));
		else
			die("Final: %s\n", response_buffer);
	}

	preexit();

	return 0;
}

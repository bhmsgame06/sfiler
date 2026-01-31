#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "serial.h"

int response_status;
char response_buffer[512];
char line_buffer[512];

/* print to serial port */
int serial_printf(int fd, char *fmt, ...) {
	char buf[1024];
	int n;

	va_list v;
	va_start(v, fmt);
	n = vsprintf(buf, fmt, v);
	va_end(v);

	write(fd, buf, n);
}

/* read line from serial port */
char *serial_read_line(int fd) {
	int i = 0;
	char c;

	do {
		read(fd, &c, 1);
		line_buffer[i++] = c;
	} while(c != '\r' && c != '\n');

	line_buffer[i] = 0;

	return line_buffer;
}

/* check response status */
static int check_response() {
	if(strcmp(response_buffer, "OK\r") == 0) {
		return RESPONSE_STATUS_OK;
	} else if(strcmp(response_buffer, "ERROR\r") == 0) {
		return RESPONSE_STATUS_ERROR;
	} else {
		return RESPONSE_UNKNOWN;
	}
}

/* read response from serial port */
int serial_response_read(int fd) {
	char c;
	do {
		read(fd, &c, 1);
	} while(c == '\r' || c == '\n');

	if(c == '+') {

		int i = 0;
		while(1) {
			read(fd, &c, 1);
			if(c == ':') break;
			response_buffer[i] = c;
			i++;
		}
		response_buffer[i] = 0;

		response_status = RESPONSE_RESULT;

	} else {

		response_buffer[0] = c;
		if(c == '\r' || c == '\n') {
			response_buffer[1] = 0;
		} else {
			int i = 1;
			do {
				read(fd, &c, 1);
				response_buffer[i] = c;
				i++;
			} while(c != '\r' && c != '\n');
			response_buffer[i] = 0;
		}

		response_status = check_response();

	}

	return response_status;
}

/* read single argument from response string from serial port */
int serial_response_read_arg(int fd) {
	int last_arg = 0;

	char c;
	int parsed = 0;
	int quoted = 0;
	int begin = 0;
	int i = 0;

	while(1) {
		read(fd, &c, 1);

		if(c == '\r') {
			last_arg = 1;
			break;
		}

		if(!quoted && c == ',') {
			break;
		}

		if(!parsed) {

			if(!quoted) {
				if(c != ' ')
					begin = 1;
				else
					begin = 0;
			}

			if(c == '"') {

				if(!quoted) {
					quoted = 1;
					begin = 1;
				} else {
					quoted = 0;
					parsed = 1;
				}

			} else {

				if(begin) {
					response_buffer[i++] = c;
				}

			}

		}
	}
	
	response_buffer[i] = 0;

	return last_arg;
}

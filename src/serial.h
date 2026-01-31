enum {
	RESPONSE_UNKNOWN = -1,
	RESPONSE_RESULT,
	RESPONSE_STATUS_OK,
	RESPONSE_STATUS_ERROR
};

extern int response_status;
extern char response_buffer[512];
extern char line_buffer[512];

/* print to serial port */
extern int serial_printf(int fd, char *fmt, ...);

/* read line from serial port */
extern char *serial_read_line(int fd);

/* read response from serial port */
extern int serial_response_read(int fd);

/* read single argument from response string from serial port */
extern int serial_response_read_arg(int fd);

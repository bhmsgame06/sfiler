struct progress {
	char *filename;
	int file_length;
	int total_transmitted;
	int show_progress;
};

/* start tracking the speed of transmission.
 * should be called in a separate thread */
extern void *progress_track_start(struct progress *ctx);

/* stop tracking the speed of transmission */
extern void progress_track_stop(struct progress *ctx);

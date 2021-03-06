#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

#include "bgio.h"
#include "util.h"
#include "log.h"


int g_highest_fd;


int find_highest_fd()
{
	int i, err;

	// Assume we'll never need more than 64 fds
	for(i=64; i; i--) {
		err = fcntl(i, F_GETFL);
		if(err != -1) {
			return i;
		}
	}
	
	assert(!"WTF?  No FDs??");
	return 0;
}


void fdcheck()
{
	int now = find_highest_fd();
	if(now != g_highest_fd) {
		fprintf(stderr, "ERROR: On forking, highest fd should be %d but it's %d\n", g_highest_fd, now);
		// can't send it to logfile because logfile has already been closed.
		// Keep running because it's not a fatal error.
	}
}


int chdir_to_dldir()
{
	int succ = 0;

	if(download_dir) {
		succ = chdir(download_dir);
		if(succ != 0) {
			log_err("Could not chdir to \"%s\": %s",
					download_dir, strerror(errno));
			fprintf(stderr, "Could not chdir to \"%s\": %s\n",
					download_dir, strerror(errno));
		}
	}

	return succ;
}


int get_window_width()
{
	return bgio_get_window_width();
}


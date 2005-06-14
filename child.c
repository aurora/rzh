/* child.c
 * 13 June 2005
 * Scott Bronson
 *
 * Starts up the zmodem child process and splices it into the data stream.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "fifo.h"
#include "io/io.h"
#include "log.h"
#include "pipe.h"
#include "child.h"
#include "util.h"


// This should trigger the receive code on the child.
// Should be effectively the same as the data that zscan ate.
static const char *startstr = "**\030B00";


// Saves existing state so it can be restored when rz finishes.
static struct pipe *save_master_read_pipe;
static struct pipe *save_master_write_pipe;
static struct pipe *save_stdout_write_pipe;
static io_proc save_stdin_atom_proc;
static fifo_proc save_master_output_fifo_proc;

// These atoms are set up by echo.  This is where we splice
// the rz proces into the data stream.
pipe_atom a_master;	// bidirectional to master
pipe_atom a_stdin;	// reads from host's stdin
pipe_atom a_stdout;	// writes to host's stdout

// pipes needed to run data to/from the master
struct pipe p_input_master;     // stdin/child -> master
struct pipe p_master_output;    // master -> stdout/child

int rz_child_pid;	// needed by the shared sigchld handler
int g_slave_fd;		// must close when forking

// the atoms required
static pipe_atom a_chin;
static pipe_atom a_chout;
static io_atom a_cherr;

// We need a pipe to send progress information to stdout
static struct pipe p_progress_stdout;


static void parse_typing(const char *buf, int len)
{
	char s[256];

	snprintf(s, sizeof(s), "KEY: len=%d <<%.*s>>\r\n", len, len, buf);

	log_dbg("TYPING (%d chars): %.*s", len, len, buf);
	pipe_write(&p_progress_stdout, s, strlen(s));
}


static void typing_io_proc(io_atom *atom, int flags)
{
	char buf[128];
	int cnt;

	if(flags != IO_READ) {
		log_warn("Got flags=%d in parse_typing_proc!");
		if(!(flags & IO_READ)) {
			return;
		}
	}

	do {
		errno = 0;
		cnt = read(atom->fd, buf, sizeof(buf));
	} while(cnt == -1 && errno == EINTR);

	if(cnt > 0) {
		parse_typing(buf, cnt);
	} else if(cnt == 0) {
		log_warn("TYPING 0 read???");
	} else {
		log_warn("TYPING read error: %d (%s)", errno, strerror(errno));
	}
}


static void cherr_proc(io_atom *atom, int flags)
{
	char buf[512];
	int cnt;

	if(flags != IO_READ) {
		log_warn("Got flags=%d in cherr_proc!");
		if(!(flags & IO_READ)) {
			return;
		}
	}

	do {
		errno = 0;
		cnt = read(atom->fd, buf, sizeof(buf));
	} while(cnt == -1 && errno == EINTR);

	if(cnt > 0) {
		log_warn("CHILD STDERR: <<<%.*s>>>", cnt, buf);
	} else if(cnt == 0) {
		log_warn("CHILD STDERR 0 read???");
	} else {
		log_warn("CHILD STDERR read error: %d (%s)", errno, strerror(errno));
	}
}


void stop_rz_child()
{
	log_dbg("Got sigchild, stopped transfer");
	fprintf(stderr, "Got sigchild, stopped transfer\n");

	io_del(&a_chin.atom);
	a_chin.atom.fd = 0;
	a_chin.atom.proc = NULL;
	a_chin.read_pipe = a_chin.write_pipe = NULL;

	io_del(&a_chout.atom);
	a_chout.atom.fd = 0;
	a_chout.atom.proc = NULL;
	a_chout.read_pipe = a_chout.write_pipe = NULL;

	io_del(&a_cherr);
	a_cherr.fd = 0;
	a_cherr.proc = NULL;

	a_master.read_pipe = save_master_read_pipe;
	a_master.write_pipe = save_master_write_pipe;
	a_stdout.write_pipe = save_stdout_write_pipe;
	a_stdin.atom.proc = save_stdin_atom_proc;

	p_input_master.read_atom = &a_stdin;
	p_input_master.block_read = 0;
    io_enable(&p_input_master.read_atom->atom, IO_READ);

	p_master_output.write_atom = &a_stdout;
	p_master_output.fifo.proc = save_master_output_fifo_proc;

	rz_child_pid = 0;
}



/** Splices the given fds into the master's data stream.
 *  Also splices keyboard interpretation into stdin and
 *  progress display into stdout.
 */

static void splice(int chstdin, int chstdout, int chstderr)
{
	int len, cnt;

	pipe_atom_init(&a_chin, chstdin);
	pipe_atom_init(&a_chout, chstdout);
	io_add(&a_chin.atom, 0);
	io_add(&a_chout.atom, IO_READ);

	save_master_read_pipe = a_master.read_pipe;
	save_master_write_pipe = a_master.write_pipe;
	save_stdout_write_pipe = a_stdout.write_pipe;
	save_stdin_atom_proc = a_stdin.atom.proc;

	// Splice child into input->master stream
	p_input_master.read_atom = &a_chout;
	a_chout.read_pipe = &p_input_master;
	// New reader so reset the read status
	p_input_master.block_read = 0;
	io_enable(&p_input_master.read_atom->atom, IO_READ);

	// Splice child into master->outpout stream
	p_master_output.write_atom = &a_chin;
	a_chin.write_pipe = &p_master_output;
	// No need to keep scanning for zmodem start string
	save_master_output_fifo_proc = p_master_output.fifo.proc;
	p_master_output.fifo.proc = NULL;
	// Since zscan ate the start string, re-insert it.
	len = strlen(startstr);
	cnt = pipe_prepend(&p_master_output, startstr, len);
	if(cnt != len) {
		fprintf(stderr, "Could not prepend to pipe.\n");
		bail(66);
	}

	// redirect stdin to go to the typing parser
	io_set(&a_stdin.atom, IO_READ);
	a_stdin.atom.proc = typing_io_proc;

	// and attach a progress meter pipe to stdout
	pipe_init(&p_progress_stdout, NULL, &a_stdout, 1024);
	pipe_write(&p_progress_stdout, "Off we go!\r\n", 11);

	// Finally, sanity checking on stderr
	io_atom_init(&a_cherr, chstderr, cherr_proc);
	io_add(&a_cherr, IO_READ);
}


/** Forks the zmodem receive process
 */

void start_proc(const char *buf, int size, void *refcon)
{
	extern void fdcheck();

	int chstdin[2];
	int chstdout[2];
	int chstderr[2];

	if(pipe(chstdin) < 0) {
		perror("creating output pipes");
		bail(77);
	}
	if(pipe(chstdout) < 0) {
		perror("creating input pipes");
		bail(78);
	}
	if(pipe(chstderr) < 0) {
		perror("creating input pipes");
		bail(78);
	}

	log_dbg("FD chstdin: rd=%d wr=%d", chstdin[0], chstdin[1]);
	log_dbg("FD chstdout: rd=%d wr=%d", chstdout[0], chstdout[1]);
	log_dbg("FD chstderr: rd=%d wr=%d", chstderr[0], chstderr[1]);

	rz_child_pid = fork();
	if(rz_child_pid < 0) {
		perror("forking receive process");
		return;
	}
	if(rz_child_pid == 0) {
		close(chstdin[1]);
		close(chstdout[0]);
		close(chstderr[0]);
	
		dup2(chstdin[0], 0);
		dup2(chstdout[1], 1);
		dup2(chstderr[1], 2);

		close(chstdin[0]);
		close(chstdout[1]);
		close(chstderr[1]);

		close(a_master.atom.fd);
		close(g_slave_fd);
		log_close();
		io_exit();

		fdcheck();

		execl("/usr/bin/rz", "rz", 0);
		fprintf(stderr, "Could not exec /usr/bin/rz: %s\n",
				strerror(errno));
		exit(89);
	}

	close(chstdin[0]);
	close(chstdout[1]);
	close(chstderr[1]);

	splice(chstdin[1], chstdout[0], chstderr[0]);
}

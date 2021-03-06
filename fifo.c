/* fifo.c
 * Scott Bronson
 * 14 Jan 2004
 *
 * Implements a fixed-size fifo buffer.  This can be used to smooth out
 * a bursty stream.
 *
 * This file is released under the MIT license.  This is basically the
 * same as public domain, but absolves the author of liability.
 * 
 * TODO: This is only half-finished.  Now that it's pretty clear what
 * the requirements are, the API should really be cleaned up.  Especially
 * the stupid filter proc.
 */

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "fifo.h"
#include "log.h"


#define LOG_BUFFER_CONTENTS 0


/* name is an arbitrary name for the fifo */
struct fifo *fifo_init(struct fifo *f, int initsize)
{
	f->size = initsize;
	f->beg = f->end = 0;
	f->buf = (char*)malloc(initsize);
	f->proc = NULL;
	if(f->buf == NULL) return NULL;

	return f;
}


void fifo_destroy(struct fifo *f)
{
	free(f->buf);
}


/* erase all data in the fifo */
void fifo_clear(struct fifo *f)
{
	f->beg = f->end = 0;
}


/* returns the number of bytes in the fifo */
// TODO can get rid of first f->size right?
int fifo_count(struct fifo *f)
{
	return (f->size + f->end - f->beg) % f->size;
}


/* returns the amount of free space left in the fifo */
// TODO can get rid of first f->size right?
int fifo_avail(struct fifo *f)
{
	return (f->size + f->beg - f->end - 1) % f->size;
}


/* dangerously add a character to the fifo */
/* make sure there's room before calling! */
void fifo_unsafe_addchar(struct fifo *f, char c)
{
	f->buf[f->end++] = c;
	if(f->end == f->size) f->end = 0;
}


/* dangerously remove a character from the fifo */
/* make sure there's data in the fifo before calling! */
int fifo_unsafe_getchar(struct fifo *f)
{
	int c = f->buf[f->beg++];
	if(f->beg == f->size) f->beg = 0;
	return c;
}


/* dangerously add a block of data to the fifo */
/* make sure there's room before calling! */
void fifo_unsafe_append(struct fifo *f, const char *buf, int cnt)
{
	if(f->end + cnt > f->size) {
		int n = f->size - f->end;
		memcpy(f->buf+f->end, buf, n);
		memcpy(f->buf, buf+n, cnt - n);
	} else {
		memcpy(f->buf+f->end, buf, cnt);
	}

	f->end = (f->end + cnt) % f->size;
}


/* dangerously add a block before the data in the fifo */
/* make sure there's room before calling! */
void fifo_unsafe_prepend(struct fifo *f, const char *buf, int cnt)
{
	if(f->beg < cnt) {
		int n = cnt - f->beg;
		memcpy(f->buf, buf + n, f->beg);
		memcpy(f->buf + f->size - n, buf, n);
		f->beg = f->size - n;
	} else {
		f->beg -= cnt;
		memcpy(f->buf + f->beg, buf, cnt);
	}
}


/* dangerously removes a block of data from the fifo */
/* make sure there's data in the fifo before calling! */
void fifo_unsafe_unpend(struct fifo *f, char *buf, int cnt)
{
	if(f->beg + cnt > f->size) {
		int n = f->size - f->beg;
		memcpy(buf, f->buf+f->beg, n);
		memcpy(buf+n, f->buf, cnt - n);
	} else {
		memcpy(buf, f->buf+f->beg, cnt);
	}

	f->beg = (f->beg + cnt) % f->size;
}

/*
static void print_fifo(fifo *f)
{
	printf("fifo at %08lX  ", (long)f);
	printf("%s  ", f->name);
	printf("beg=%d end=%d size=%d", (int)f->beg, (int)f->end, (int)f->size);
	printf("  count=%d avail=%d\r\n", (int)fifo_count(f), (int)fifo_avail(f));
}
*/


const char* sanitize(const char *s, int n)
{
	int i;
	static char buf[256];
	char *cp = buf;

	if(n > sizeof(buf)/4-1) {
		n = sizeof(buf)/4-1;
	}

	for(i=0; i<n; i++) {
		if(s[i] < 32 || s[i] >= 127) {
			*cp++ = '\\';
			*cp++ = (((unsigned char)s[i] >> 6) & 0x07) + '0';
			*cp++ = (((unsigned char)s[i] >> 3) & 0x07) + '0';
			*cp++ = (((unsigned char)s[i] >> 0) & 0x07) + '0';
		} else if(s[i] == '"') {
			*cp++ = '\\';
			*cp++ = '"';
		} else {
			*cp++ = s[i];
		}
	}
	*cp = '\0';

	return buf;
}


#ifdef NDEBUG
#define logio(x,y,z,a,b,c)
#else
void logio(char *gr1, char* gr2, int fd, const char *buf, int cnt, int act)
{
	int n = act, i;

	if(n >= 0) {
		// print the first few bytes.
		i = n;
		if(i > 16) {
			i = 16;
		}
		n -= i;
		log_info("%s %d bytes %s %d: (%d)\t\t\"%s\"%s",
				gr1, act, gr2, fd, cnt, sanitize(buf, i), n>0?"...":"");
		buf += i;

		// print the rest of the buffer
		while(LOG_BUFFER_CONTENTS && n > 0) {
			i = n;
			if(i > 64) {
				i = 64;
			}

			n -= i;
			log_info("\t\t\"%s\"%s", sanitize(buf, i), n>0?"...":"");
			buf += i;
		}

	} else {
		log_err("%s error from %d: %d (%s)",
				gr1, fd, errno, strerror(errno));
	}
}
#endif


/** Partially fill the fifo by calling read().
 *
 * @returns the number of bytes read (0 is a valid number; it means
 * that the filter proc ate all the data).
 *
 * TODO:
 *  Only performs a single read call.  We should probably read to exhaustion.
 *  So, the first read must not return EAGAIN.  After the first read, we keep
 *  reading until we get EAGAIN.
 *
 * TODO: get rid of the copy.
 * TODO: make it resize the fifo if necessary to try to exhaust the read
 */

int fifo_read(struct fifo *f, int fd)
{
	char buf[BUFSIZ];
	int cnt, n;

	cnt = fifo_avail(f);
	if(cnt > sizeof(buf)) {
		cnt = sizeof(buf);
	}

	do {
		errno = 0;
		n = read(fd, buf, cnt);
		if(n == -1) {
			log_dbg("Error reading %d for fifo: %d (%s)", fd, errno, strerror(errno));
		}
	} while(n == -1 && errno == EINTR);

	logio("Read", "from", fd, buf, cnt, n);
	cnt = n;

	if(cnt < 0) {
		// We had better not be told that there's no data to read!
		// We just received an event telling us that there was.
		//
		// Hm.  It appears that all the read fds return read events
		// when a sigchild happens, but none of them actually have data?
		// assert(errno != EAGAIN);
	} else if(cnt == 0) {
		// eof request
		cnt = -2;
	}


	if(f->proc) {
		int old = fifo_avail(f);
		(*f->proc)(f, buf, cnt, fd);
		if(cnt > 0) {
			cnt = old - fifo_avail(f);
		}
		if(cnt >= 0) {
			log_info("RProc copied %d into %d, count is now %d.", cnt, fd,
					fifo_count(f));
		} else {
			log_err("RProc returned %d for %d.", cnt, fd);
		}
	} else {
		// copy the read data into the buffer
		if(cnt > 0) {
			fifo_unsafe_append(f, buf, cnt);
		}
	}

	return cnt;
}


/** Attempt to empty the fifo by calling write().
 *  
 * @returns the number of bytes written or -1 if there was an error.
 * This routine should never return 0 but I can't guarantee it.
 */

#define logwr(fd, buf, cnt, act) logio("Write", "to", fd, buf, cnt, act)

int fifo_write(struct fifo *f, int fd)
{
	int cnt = 0;
	int n;

	if(f->beg < f->end) {
		do {
			errno = 0;
			cnt = write(fd, f->buf+f->beg, f->end-f->beg);
			logwr(fd, f->buf+f->beg, f->end-f->beg, cnt);
		} while(cnt == -1 && errno == EINTR);
		if(cnt > 0) f->beg += cnt;
	} else if(f->beg > f->end) {
		do {
			errno = 0;
			cnt = write(fd, f->buf+f->beg, f->size-f->beg);
			logwr(fd, f->buf+f->beg, f->size-f->beg, cnt);
		} while(cnt == -1 && errno == EINTR);
		if(cnt > 0) {
			f->beg = (f->beg + cnt) % f->size;
			if(f->beg == 0) {
				do {
					errno = 0;
					n = write(fd, f->buf, f->end);
					logwr(fd, f->buf, f->end, n);
				} while(n == -1 && errno == EINTR);
				if(n > 0) { f->beg = n; cnt += n; }
			}
		}
	}

	return cnt;
}


/* copies as much of the contents of one fifo as possible
 * to the other */

int fifo_copy(struct fifo *src, struct fifo *dst)
{
	int cnt = fifo_count(src);
	int ava = fifo_avail(dst);
	if(ava < cnt) cnt = ava;

	if(src->beg + cnt > src->size) {
		int n = src->size - src->beg;
		fifo_unsafe_append(dst, src->buf+src->beg, n);
		fifo_unsafe_append(dst, src->buf, cnt - n);
	} else {
		fifo_unsafe_append(dst, src->buf+src->beg, cnt);
	}

	src->beg = (src->beg + cnt) % src->size;
	return cnt;
}


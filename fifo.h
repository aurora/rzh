/* fifo.h
 * Scott Bronson
 * 14 Jan 2004
 *
 * This file is released under the MIT license.  This is basically the
 * same as public domain, but absolves the author of liability.
 *
 * todo: add dynamic resizing.
 */

struct fifo;

typedef void (*fifo_proc)(struct fifo *ff, const char *buf, int size, int fd);

struct fifo {
	char *buf;
	int beg, end;
	int size;
	fifo_proc proc;
	void *refcon;
};


/* fifo states */
#define FIFO_UNUSED 0
#define FIFO_IN_USE 1
#define FIFO_CLOSED 2


#define fifo_empty(f) 		((f)->beg == (f)->end)


/* allocates a fifo initialially able to hold initsize chars
 * and will grow to hold maxsize chars if needed.
 * Returns NULL if fifo memory couldn't be allocated.
 */
struct fifo* fifo_init(struct fifo *f, int initsize);
void fifo_destroy(struct fifo *f);

void fifo_clear(struct fifo *f);      /* empty the fifo of all data */
int fifo_count(struct fifo *f);    /* number of bytes of data in the fifo */
int fifo_avail(struct fifo *f);    /* free bytes left in the fifo */

void fifo_unsafe_addchar(struct fifo *f, char c);
int fifo_unsafe_getchar(struct fifo *f);

/* stuff a memory block into the fifo */
void fifo_unsafe_append(struct fifo *f, const char *buf, int cnt);
#define fifo_unsafe_append_str(f, str) fifo_unsafe_append(f, str, strlen(str))
void fifo_unsafe_prepend(struct fifo *f, const char *buf, int cnt);
#define fifo_unsafe_prepend_str(f, str) fifo_unsafe_prepend(f, str, strlen(str))

/* grab a memory block out of the fifo */
void fifo_unsafe_unpend(struct fifo *f, char *buf, int cnt);
#define fifo_unsafe_unpend_str(f, str) fifo_unsafe_unpend(f, str, strlen(str))

/* fill the fifo by calling read() */
int fifo_read(struct fifo *f, int fd);
/* empty the fifo by calling write() */
int fifo_write(struct fifo *f, int fd);
/* copy as much of the data from src as will fit into dst */
int fifo_copy(struct fifo *src, struct fifo *dst);

/* try to flush the fifo to its fd */
void fifo_flush(struct fifo *f);


// general purpose debug routine:
const char* sanitize(const char *s, int n);

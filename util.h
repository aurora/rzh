// This is a file of random crap that doesn't easily fit anywhere else.

extern int g_highest_fd;
extern int opt_quiet;

extern const char *download_dir;

void fdcheck();
int find_highest_fd();
int get_window_width();

// provided by rzh.
extern void bail(int val);
void rzh_fork_prepare();
int chdir_to_dldir();

// result codes returned by exit().
// actually, these are mostly used by main, not bgio

enum {
	argument_error=1,
	runtime_error=2,
	fork_error1=5,
	fork_error2=6,
	fork_error3=7,
};


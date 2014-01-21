#include "config.h"
#include "init-frame.h"
#include <stdio.h>

bool main_terminate= false;
const char *main_cfgfile= NULL;
const char *main_exec_on_exit= NULL;
bool main_use_stdin= false;
bool main_mlockall= false;
bool main_failsafe= false;
int main_loglevel= LOG_LEVEL_INFO;
wake_t main_wake;

wake_t *wake= &main_wake;

// Parse options and apply to global vars; return false if fails
void parse_opts(char **argv);
void parse_option(char shortname, char* longname, char ***argv);

int main(int argc, char** argv) {
	int wstat, f;
	pid_t pid;
	struct timeval tv;
	bool config_on_stdin= false;
	service_t *svc;
	controller_t *ctl;
	wake_t wake_instance;
	
	memset(&wake_instance, 0, sizeof(wake_instance));
	wake= &wake_instance;
	
	// Special defaults when running as init
	if (getpid() == 1) {
		main_cfgfile= CONFIG_FILE_DEFAULT_PATH;
		main_failsafe= true;
	}
	
	// parse arguments, overriding default values
	parse_opts(argv+1);
	
	// Set up signal handlers and signal mask and signal self-pipe
	sig_init();
	// Initialize file descriptor object pool and indexes
	fd_init(FD_POOL_SIZE, FD_OBJ_SIZE);
	// Initialize service object pool and indexes
	svc_init(SERVICE_POOL_SIZE, SERVICE_OBJ_SIZE);
	// Initialize controller object pool
	ctl_init();
	
	// A handle to dev/null is mandatory...
	f= open("/dev/null", O_RDWR);
	if (f < 0) {
		fatal(EXIT_INVALID_ENVIRONMENT, "Can't open /dev/null: %d", errno);
		// if we're in failsafe mode, fatal doesn't exit()
		log_error("Will use stderr instead of /dev/null!");
		f= 2;
	}
	
	fd_assign("null", f, true, "/dev/null");

	if (main_cfgfile) {
		if (0 == strcmp(main_cfgfile, "-"))
			config_on_stdin= true;
		else {
			f= open(main_cfgfile, O_RDONLY|O_NONBLOCK|O_NOCTTY);
			if (f == -1)
				fatal(EXIT_INVALID_ENVIRONMENT, "failed to open config file \"%s\": %d",
					main_cfgfile, errno);
			else if (!(ctl= ctl_new(f, -1))) {
				close(f);
				fatal(EXIT_BROKEN_PROGRAM_STATE, "failed to allocate controller for config file!");
			}
			else
				ctl_set_auto_final_newline(ctl, true);
		}
	}
	
	if (main_use_stdin || config_on_stdin) {
		if (!(ctl= ctl_new(0, 1)))
			fatal(3, "failed to initialize stdio controller client!");
		else
			ctl_set_auto_final_newline(ctl, config_on_stdin);
	}
	
	if (main_mlockall) {
		// Lock all memory into ram. init should never be "swapped out".
		if (mlockall(MCL_CURRENT | MCL_FUTURE))
			perror("mlockall");
	}
	
	// terminate is disabled when running as init, so this is an infinite loop
	// (except when debugging)
	wake->now= gettime_mon_frac();
	while (!main_terminate) {
		// set our wait parameters so other methods can inject new wake reasons
		wake->next= wake->now + (200LL<<32); // wake at least every 200 seconds
		FD_ZERO(&wake->fd_read);
		FD_ZERO(&wake->fd_write);
		FD_ZERO(&wake->fd_err);
		
		// signals off
		sigset_t maskall, old;
		sigfillset(&maskall);
		if (!sigprocmask(SIG_SETMASK, &maskall, &old) == 0)
			perror("sigprocmask(all)");
	
		// report signals and set read-wake on signal fd
		sig_run(wake);
		
		// reap all zombies, possibly waking services
		while ((pid= waitpid(-1, &wstat, WNOHANG)) > 0) {
			log_trace("waitpid found pid = %d", (int)pid);
			if ((svc= svc_by_pid(pid)))
				svc_handle_reaped(svc, wstat);
			else
				log_trace("pid does not belong to any service");
		}
		if (pid < 0)
			log_trace("waitpid: errno= %m");
		
		// run controller state machine
		ctl_run(wake);
		
		// run state machine of each service that is active.
		svc_run_active(wake);
		
		ctl_flush(wake);
		
		// resume normal signal mask
		if (!sigprocmask(SIG_SETMASK, &old, NULL) == 0)
			perror("sigprocmask(reset)");

		// Wait until an event or the next time a state machine needs to run
		// (state machines edit wake.next)
		// If we don't need to wait, don't.  (we don't care about select() return code)
		wake->now= gettime_mon_frac();
		if (wake->next - wake->now > 0) {
			tv.tv_sec= (long)((wake->next - wake->now) >> 32);
			tv.tv_usec= (long)((((wake->next - wake->now)&0xFFFFFFFFLL) * 1000000) >> 32);
			if (select(wake->max_fd, &wake->fd_read, &wake->fd_write, &wake->fd_err, &tv) < 0) {
				// shouldn't ever fail, but if not EINTR, at least log it and prevent
				// looping too fast
				if (errno != EINTR) {
					perror("select");
					tv.tv_usec= 500000;
					tv.tv_sec= 0;
					select(0, NULL, NULL, NULL, &tv); // use it as a sleep, this time
				}
			}
		}
	}
	
	if (main_exec_on_exit)
		fatal(0, "terminated normally");
	return 0;
}

// returns monotonic time as a 32.32 fixed-point number 
int64_t gettime_mon_frac() {
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
		t.tv_sec= time(NULL);
		t.tv_nsec= 0;
	}
	return (int64_t) (
		(((uint64_t) t.tv_sec) << 32)
		| (((((uint64_t) t.tv_nsec)<<32) / 1000000000 ) & 0xFFFFFFFF)
	);
}

void parse_opts(char **argv) {
	char *current;
	
	while ((current= *argv++)) {
		if (current[0] == '-' && current[1] == '-') {
			parse_option(0, current+2, &argv);
		}
		else if (current[0] == '-') {
			for (++current; *current; current++)
				parse_option(*current, NULL, &argv);
		}
		else {
			// if failsafe, will not exit()
			fatal(EXIT_BAD_OPTIONS, "Unexpected argument \"%s\"\n", current);
		}
	}
}

void set_opt_verbose(char** argv)     { main_loglevel--;      }
void set_opt_quiet(char** argv)       { main_loglevel++;      }
void set_opt_stdin(char **argv)       { main_use_stdin= true; }
void set_opt_mlockall(char **argv)    { main_mlockall= true;  }
void set_opt_failsafe(char **argv)    { main_failsafe= true;  }
void set_opt_configfile(char** argv ) {
	struct stat st;
	if (stat(argv[0], &st))
		fatal(EXIT_BAD_OPTIONS, "Cannot stat configfile \"%s\"", argv[0]);
	main_cfgfile= argv[0];
}
void set_opt_exec_on_exit(char **argv) {
	struct stat st;
	if (stat(argv[0], &st))
		fatal(EXIT_BAD_OPTIONS, "Cannot stat exec-on-exit program \"%s\"", argv[0]);
	main_exec_on_exit= argv[0];
}

const struct option_table_entry_s {
	char shortname;
	const char *longname;
	int argc;
	void (*handler)(char **argv);
} option_table[]= {
	{ 'v', "verbose",      0, set_opt_verbose },
	{ 'q', "quiet",        0, set_opt_quiet },
	{ 'c', "config-file",  1, set_opt_configfile },
	{  0 , "stdin",        0, set_opt_stdin },
	{ 'M', "mlockall",     0, set_opt_mlockall },
	{ 'F', "failsafe",     0, set_opt_failsafe },
	{ 'E', "exec-on-exit", 1, set_opt_exec_on_exit },
	{ 0, NULL, 0, NULL }
};

void parse_option(char shortname, char* longname, char ***argv) {
	const struct option_table_entry_s *entry;
	int i;
	
	for (entry= option_table; entry->handler; entry++) {
		if ((shortname && (shortname == entry->shortname))
			|| (longname && (0 == strcmp(longname, entry->longname)))
		) {
			for (i= 0; i < entry->argc; i++)
				if (!(*argv)[i]) {
					fatal(EXIT_BAD_OPTIONS, "Missing argument for -%c%s",
						longname? '-' : shortname, longname? longname : "");
				}
			entry->handler(*argv);
			(*argv)+= entry->argc;
			return;
		}
	}
	fatal(EXIT_BAD_OPTIONS, "Unknown option -%c%s", longname? '-' : shortname, longname? longname : "");
}

void fatal(int exitcode, const char *msg, ...) {
	char buffer[1024];
	int i;
	va_list val;
	va_start(val, msg);
	vsnprintf(buffer, sizeof(buffer), msg, val);
	va_end(val);
	
	if (main_exec_on_exit) {
		// Pass params to child as environment vars
		setenv("INIT_FRAME_ERROR", buffer, 1);
		sprintf(buffer, "%d", exitcode);
		setenv("INIT_FRAME_EXITCODE", buffer, 1);
		// Close all nonstandard FDs
		for (i= 3; i < FD_SETSIZE; i++) close(i);
		// exec child
		execl(main_exec_on_exit, main_exec_on_exit, NULL);
		log_error("Unable to exec \"%s\": %d", main_exec_on_exit, errno);
		// If that failed... continue?
	}
	
	if (main_failsafe) {
		fprintf(stderr, "fatal (but attempting to continue): %s\n", buffer);
	} else {
		fprintf(stderr, "fatal: %s\n", buffer);
		exit(exitcode);
	}
}

void log_write(int level, const char *msg, ...) {
	if (main_loglevel > level)
		return;
	
	const char *prefix= level > LOG_LEVEL_INFO? (level > LOG_LEVEL_WARN? "error":"warning")
		: (level > LOG_LEVEL_DEBUG? "info" : (level > LOG_LEVEL_TRACE? "debug":"trace"));
	
	char msg2[256];
	if (snprintf(msg2, sizeof(msg2), "%s: %s\n", prefix, msg) >= sizeof(msg2))
		memset(msg2+sizeof(msg2)-4, '.', 3); // append "..." if pattern doesn't fit.  But it always should.
	
	va_list val;
	va_start(val, msg);
	vfprintf(stderr, msg2, val);
	va_end(val);
}

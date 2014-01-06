#ifndef INIT_FRAME_H
#define INIT_FRAME_H

// Get CLOCK_MONOTONIC microseconds as int64_t
int64_t gettime_us();

typedef struct wake_s {
	fd_set fd_read, fd_write, fd_err;
	int max_fd;
	// all times in microseconds
	// times might wrap!  never compare times with > >= < <=, only differences.
	int64_t now;  // current time according to main loop
	int64_t next; // time when we next need to process something
} wake_t;

extern bool main_terminate;

//----------------------------------------------------------------------------
// controller.c interface

struct controller_s;
typedef struct controller_s controller_t;

// Initialize controller state machine.
// if cfg_file is NULL, skip reading config file
// if controller_script is NULL, no controller wil be used.
// if controller_script is "-", controller is on stdin/stdout
void ctl_init(const char* cfg_file, bool use_stdin);
void ctl_pipe_reset();

// Queue a message to the controller, possibly overflowing the output buffer
// and requiring a state reset event.
// If no controller is running, a sighup message will cause the config file to be re-loaded.
bool ctl_write(const char *msg, ... );
bool ctl_notify_signal(int sig_num);
bool ctl_notify_svc_start(const char *name, double waittime);
bool ctl_notify_svc_up(const char *name, double uptime, pid_t pid);
bool ctl_notify_svc_down(const char *name, double downtime, double uptime, int wstat, pid_t pid);
bool ctl_notify_svc_meta(const char *name, int meta_count, const char *meta_series);
bool ctl_notify_svc_args(const char *name, int arg_count, const char *arg_series);
bool ctl_notify_svc_fds(const char *name, int fd_count, const char *fd_series);
bool ctl_notify_fd_state(const char *name, const char *file_path, const char *pipe_read, const char *pipe_write);
#define ctl_notify_error(msg, ...) (ctl_write("error" msg "\n", ##__VA_ARGS__))

// Handle state transitions based on communication with the controller
void ctl_run(wake_t *wake);

//----------------------------------------------------------------------------
// service.c interface

struct service_s;
typedef struct service_s service_t;
extern const int min_service_obj_size;

// Initialize the service pool
void svc_init(int service_count, int size_each);

const char * svc_get_name(service_t *svc);

bool svc_notify_state(service_t *svc, wake_t *wake);

// Set metadata for a service.  Creates the service if it doesn't exist.
// Fails if the metadata + env + argv + fd is longer than the allocated buffer.
bool svc_set_meta(const char *name, const char *tsv_fields);
void svc_get_meta(service_t *svc, int *n_strings, const char **string_series);

// Set args for a service.  Creates the service if it doesn't exist.
// Fails if the metadata + env + argv + fd is longer than the allocated buffer.
bool svc_set_argv(const char *name, const char *tsv_fields);
void svc_get_argv(service_t *svc, int *n_strings, const char **string_series);

// Set env for a service.  Created the service if it doesn't exist.
// Fails if the metadata + env + argv + fd is longer than the allocated buffer.
//bool svc_set_env(const char *name, const char *tsv_fields);

// Set file descriptors for a service.  Create the service if it doesn't exist.
// Fails if the metadata + env + argv + fd is longer than the allocated buffer.
bool svc_set_fds(const char *name, const char *tsv_fields);
void svc_get_fds(service_t *svc, int *n_strings, const char **string_series);

// update service state machine when requested to start
void svc_handle_exec(service_t *svc);

// update service state machine after PID is reaped
void svc_handle_reap(service_t *svc, int wstat);

// run an iteration of the state machine for the service
void svc_run(service_t *svc, wake_t *wake);

// Lookup services by attributes
service_t * svc_by_name(const char *name, bool create);
service_t * svc_by_pid(pid_t pid);
service_t * svc_iter_next(service_t *current, const char *from_name);

// Deallocate service struct
void svc_delete(service_t *svc);

//----------------------------------------------------------------------------
// fd.c interface

struct fd_s;
typedef struct fd_s fd_t;
extern const int min_fd_obj_size;

// Initialize the fd pool from a static chunk of memory
void fd_init(int fd_count, int size_each);

const char* fd_get_name(fd_t *);
const char* fd_get_file_path(fd_t *);
const char* fd_get_pipe_read_end(fd_t *fd);
const char* fd_get_pipe_write_end(fd_t *fd);

// Open a pipe from one named FD to another
// returns a ref to the write-end, which has a pointer to the read-end.
fd_t * fd_pipe(const char *name1, const char *name2);

// Open a file on the given name, possibly closing a handle by that name
fd_t * fd_open(const char *name, char *path, char *opts);

// Close a named handle
bool fd_close(const char *name);

void fd_dump_state(fd_t *fd);

fd_t * fd_by_name(const char *name);
fd_t * fd_by_fd(int fd);
fd_t * fd_iter_next(fd_t *current, const char *from_name);

void fd_delete(fd_t *fd);

//----------------------------------------------------------------------------
// signal.c interface

// Initialize signal-related things
void sig_init();

// Deliver any caught signals as messages to the controller,
// and set a wake for reads on the signal fd
void sig_run(wake_t *wake);

const char * sig_name(int sig_num);

#endif

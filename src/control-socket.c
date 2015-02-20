/* control-socket.c - unix socket server for controller connections
 * Copyright (C) 2014  Michael Conrad
 * Distributed under GPLv2, see LICENSE
 */

#include "config.h"
#include "daemonproxy.h"

int control_socket= -1;
struct sockaddr_un control_socket_addr;

void control_socket_init() {
	memset(&control_socket_addr, 0, sizeof(control_socket_addr));
	control_socket_addr.sun_family= AF_UNIX;
}

void control_socket_run() {
	int client;
	controller_t *ctl;
	
	if (control_socket >= 0) {
		if (woke_on_readable(control_socket)) {
			log_debug("control_socket is ready for accept()");
			ctl= ctl_alloc();
			if (!ctl) {
				log_warn("No free controllers to accpet socket connection");
				if (wake->now + (5LL << 32) < wake->next)
					wake->next= wake->now + (5LL << 32);
				return;
			}
			client= accept(control_socket, NULL, NULL);
			if (client < 0) {
				log_debug("accept: %s", strerror(errno));
				ctl_free(ctl);
			}
			if (!ctl_ctor(ctl, client, client)) {
				ctl_dtor(ctl);
				ctl_free(ctl);
				close(client);
			}
		}
		wake_on_readable(control_socket);
	}
}

static bool remove_any_socket(const char *path) {
	struct stat st;
	// unlink existing socket, but only if a socket owned by our UID.
	if (stat(path, &st) < 0)
		return true;
	
	if (!(st.st_mode & S_IFSOCK) || !(st.st_uid == geteuid() || st.st_uid == geteuid()))
		return false;
	
	if (unlink(path) == 0) {
		log_info("Unlinked control socket %s", path);
		return true;
	}
	else {
		log_error("Can't unlink control socket: %s", strerror(errno));
		return false;
	}
}

bool control_socket_start(strseg_t path) {
	if (path.len >= sizeof(control_socket_addr.sun_path)) {
		errno= ENAMETOOLONG;
		return false;
	}
	if (path.len <= 0 || !path.data[0]) {
		errno= EINVAL;
		return false;
	}

	// If currently listening, clean that up first
	if (control_socket >= 0)
		control_socket_stop();
	
	// copy new name into address
	memcpy(control_socket_addr.sun_path, path.data, path.len);
	memset(control_socket_addr.sun_path+path.len, 0, sizeof(control_socket_addr.sun_path)-path.len);
	
	// If this is a new name, possibly remove any leftover socket in our way
	// Note that we nul-terminated this path be ensuring path.len was less than
	//  sizeof(sockaddr_un.sun_path)
	if (!remove_any_socket(control_socket_addr.sun_path))
		return false;
	
	// create the unix socket
	control_socket= socket(PF_UNIX, SOCK_STREAM, 0);
	if (control_socket < 0) {
		log_error("socket: %s", strerror(errno));
		return false;
	}
	
	if (bind(control_socket, (struct sockaddr*) &control_socket_addr, (socklen_t) sizeof(control_socket_addr)) < 0)
		log_error("bind(control_socket, %s: %s", control_socket_addr.sun_path, strerror(errno));
	else if (listen(control_socket, 2) < 0)
		log_error("listen(control_socket): %s", strerror(errno));
	else if (!fd_set_nonblock(control_socket))
		log_error("fcntl(control_socket, O_NONBLOCK): %s", strerror(errno));
	else {
		wake_on_readable(control_socket);
		return true;
	}
	
	control_socket_stop();
	return false;
}

void control_socket_stop() {
	if (control_socket >= 0) {
		wake_cancel_fd(control_socket);
		close(control_socket);
		control_socket= -1;
		remove_any_socket(control_socket_addr.sun_path);
	};
}

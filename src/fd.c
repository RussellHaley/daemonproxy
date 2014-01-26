#include "config.h"
#include "daemonproxy.h"
#include "Contained_RBTree.h"

// Describes a named file handle

#define FD_TYPE_UNDEF  0
#define FD_TYPE_FILE   1
#define FD_TYPE_PIPE_R 2
#define FD_TYPE_PIPE_W 3
#define FD_TYPE_INTERNAL 4

struct fd_s {
	int size;
	unsigned int
		type: 31,
		is_const: 1;
	RBTreeNode name_index_node;
	int fd;
	union {
		char *path;
		struct fd_s *pipe_peer;
		struct fd_s *next_free;
	};
	char buffer[];
};

// Define a sensible minimum for service size.
// Want at least struct size, plus room for name of fd and short filesystem path
const int min_fd_obj_size= sizeof(fd_t) + NAME_MAX * 2;

fd_t *fd_pool= NULL;
RBTree fd_by_name_index;
fd_t *fd_free_list;

void add_fd_by_name(fd_t *fd);
void create_missing_dirs(char *path);

int fd_by_name_compare(void *data, RBTreeNode *node) {
	strseg_t *name= (strseg_t*) data;
	return strncmp(name->data, ((fd_t *) node->Object)->buffer, name->len);
}

void fd_init(int fd_count, int size_each) {
	int i;
	fd_t *fd;
	fd_pool= (fd_t*) malloc(fd_count * size_each);
	if (!fd_pool)
		abort();
	memset(fd_pool, 0, fd_count*size_each);
	fd_free_list= fd_pool;
	for (i=0; i < fd_count; i++) {
		fd= (fd_t*) (((char*)fd_pool) + i * size_each);
		fd->size= size_each;
		fd->next_free= (i+1 >= fd_count)? NULL
			: (fd_t*) (((char*)fd_pool) + (i+1) * size_each);
	}
	fd_pool[fd_count-1].next_free= NULL;
	RBTree_Init( &fd_by_name_index, fd_by_name_compare );
}

const char* fd_get_name(fd_t *fd) {
	return fd->buffer;
}

int fd_get_fdnum(fd_t *fd) {
	return fd->fd;
}

const char* fd_get_file_path(fd_t *fd) {
	return fd->type == FD_TYPE_FILE? fd->path : NULL;
}

const char* fd_get_pipe_read_end(fd_t *fd) {
	return fd->type == FD_TYPE_PIPE_W && fd->pipe_peer? fd->pipe_peer->buffer : NULL;
}

const char* fd_get_pipe_write_end(fd_t *fd) {
	return fd->type == FD_TYPE_PIPE_R && fd->pipe_peer? fd->pipe_peer->buffer : NULL;
}

bool fd_notify_state(fd_t *fd) {
	switch (fd->type) {
	case FD_TYPE_INTERNAL:
	case FD_TYPE_FILE: return ctl_notify_fd_state(NULL, fd->buffer, fd->path, NULL, NULL);
	case FD_TYPE_PIPE_R: return ctl_notify_fd_state(NULL, fd->buffer, NULL, NULL, fd->pipe_peer? fd->pipe_peer->buffer : "(closed)");
	case FD_TYPE_PIPE_W: return ctl_notify_fd_state(NULL, fd->buffer, NULL, fd->pipe_peer? fd->pipe_peer->buffer : "(closed)", NULL);
	default: return ctl_notify_error(NULL, "File descriptor has invalid state");
	}
}

// Open a pipe from one named FD to another
// returns a ref to the read end, which holds a ref to the write end.
fd_t * fd_pipe(const char *name1, const char *name2) {
	int pair[2]= { -1, -1 };
	fd_t *fd1= fd_by_name((strseg_t){ name1, strlen(name1) }, true);
	fd_t *fd2= fd_by_name((strseg_t){ name2, strlen(name2) }, true);
	// If failed to allocate/find either of them, give up
	// also fail if either of them is a constant
	if (!fd1 || !fd2 || fd1->is_const || fd2->is_const)
		goto fail_cleanup;
	// Check that pipe() call succeeds
	if (pipe(pair))
		goto fail_cleanup;
	
	// If fd1 is being overwritten, close it
	if (fd1->type == FD_TYPE_UNDEF && fd1->fd >= 0)
		close(fd1->fd);
	// same for fd2
	if (fd2->type == FD_TYPE_UNDEF && fd2->fd >= 0)
		close(fd2->fd);
	
	fd1->type= FD_TYPE_PIPE_R;
	fd1->fd= pair[0];
	fd1->pipe_peer= fd2;
	fd2->type= FD_TYPE_PIPE_W;
	fd2->fd= pair[1];
	fd2->pipe_peer= fd1;
	
	fd_notify_state(fd1);
	fd_notify_state(fd2);
	return fd1;
	
	fail_cleanup:
	if (fd1 && fd1->type == FD_TYPE_UNDEF)
		fd_delete(fd1);
	if (fd2 && fd2->type == FD_TYPE_UNDEF)
		fd_delete(fd2);
	return NULL;
}

// Open a file on the given name, possibly closing a handle by that name
fd_t * fd_open(const char *name, char *path, char *opts) {
	int flags, fd, n, buf_free;
	char *start, *end;
	fd_t *fd_obj;
	bool f_read, f_write, f_mkdir;
	
	fd_obj= fd_by_name((strseg_t){ name, strlen(name) }, true);
	if (fd_obj->is_const)
		return NULL;
	
	// Now, try to perform the open
	#define STRMATCH(name) (strncmp(start, name, end-start) == 0)
	flags= O_NOCTTY;
	f_mkdir= f_read= f_write= false;
	for (start= end= opts; *end; start= end+1) {
		end= strchrnul(start, ',');
		switch (*start) {
		case 'a':
			if (STRMATCH("append")) flags |= O_APPEND;
			break;
		case 'c':
			if (STRMATCH("create")) flags |= O_CREAT;
			break;
		case 'm':
			if (STRMATCH("mkdir")) f_mkdir= true;
			break;
		case 'r':
			if (STRMATCH("read")) f_read= true;
			break;
		case 't':
			if (STRMATCH("trunc")) flags |= O_TRUNC;
			break;
		case 'w':
			if (STRMATCH("write")) f_write= true;
			break;
		case 'n':
			if (STRMATCH("nonblock")) flags |= O_NONBLOCK;
			break;
		}
	}
	flags |= f_write && f_read? O_RDWR : f_write? O_WRONLY : O_RDONLY;
	if (f_mkdir)
		create_missing_dirs(path);

	fd= open(path, flags, 600);
	if (fd < 0)
		goto fail_cleanup;
	
	// Overwrite (and possibly setup) the fd_t object
	if (fd_obj->type != FD_TYPE_UNDEF && fd_obj->fd >= 0)
		close(fd_obj->fd);
	
	fd_obj->fd= fd;
	fd_obj->type= FD_TYPE_FILE;
	
	// copy as much of path into the buffer as we can.
	fd_obj->path= fd_obj->buffer + strlen(name) + 1;
	buf_free= fd_obj->size - (fd_obj->path - (char*) fd_obj);
	n= strlen(path);
	if (n < buf_free)
		memcpy(fd_obj->path, path, n+1);
	// else truncate with "..."
	else if (3 < buf_free) {
		n= buf_free - 4;
		memcpy(fd_obj->path, path, n);
		memcpy(fd_obj->path + n, "...", 4);
	}
	// unless we don't even have 4 chars to spare, in which case we make it an empty string
	else fd_obj->path--;
	
	fd_notify_state(fd_obj);
	return fd_obj;
	
	fail_cleanup:
	if (fd_obj && fd_obj->type == FD_TYPE_UNDEF)
		fd_delete(fd_obj);
	return NULL;
}

fd_t *fd_assign(const char *name, int fd, bool is_const, const char *description) {
	int n, buf_free;
	fd_t *fd1= fd_by_name((strseg_t){ name, strlen(name) }, true);
	if (fd1->type != FD_TYPE_UNDEF && fd1->fd >= 0)
		close(fd1->fd);
	fd1->type= FD_TYPE_INTERNAL;
	fd1->fd= fd;
	fd1->is_const= is_const;
	// copy as much of path into the buffer as we can.
	fd1->path= fd1->buffer + strlen(name) + 1;
	buf_free= fd1->size - (fd1->path - (char*) fd1);
	n= strlen(description);
	if (n < buf_free)
		memcpy(fd1->path, description, n+1);
	// else truncate with "..."
	else if (3 < buf_free) {
		n= buf_free - 4;
		memcpy(fd1->path, description, n);
		memcpy(fd1->path + n, "...", 4);
	}
	// unless we don't even have 4 chars to spare, in which case we make it an empty string
	else fd1->path--;
	return fd1;
}

void create_missing_dirs(char *path) {
	char *end;
	for (end= strchr(path, '/'); end; end= strchr(end+1, '/')) {
		*end= '\0';
		mkdir(path, 0700); // would probably take longer to stat than to just let mkdir fail
		*end= '/';
	}
}

// Close a named handle
void fd_delete(fd_t *fd) {
	// disassociate from other end of pipe, if its a pipe.
	if (fd->type == FD_TYPE_PIPE_R || fd->type == FD_TYPE_PIPE_W) {
		if (fd->pipe_peer)
			fd->pipe_peer->pipe_peer= NULL;
	}
	// close descriptor.
	if (fd->fd >= 0) close(fd->fd);
	// Remove name from index
	RBTreeNode_Prune( &fd->name_index_node );
	// Clear the type
	fd->type= FD_TYPE_UNDEF;
	// and add it to the free-list.
	fd->next_free= fd_free_list;
	fd_free_list= fd;
}

fd_t * fd_by_name(strseg_t name, bool create) {
	RBTreeSearch s= RBTree_Find( &fd_by_name_index, &name );
	if (s.Relation == 0)
		return (fd_t*) s.Nearest->Object;
	if (create && fd_free_list && name.len < fd_free_list->size - sizeof(fd_t)) {
		fd_t *ret= fd_free_list;
		fd_free_list= fd_free_list->next_free;
		int obj_size= ret->size;
		memset(ret, 0, obj_size);
		ret->size= obj_size;
		ret->type= FD_TYPE_UNDEF;
		memcpy(ret->buffer, name.data, name.len);
		ret->buffer[name.len]= '\0';
		RBTreeNode_Init( &ret->name_index_node );
		ret->name_index_node.Object= ret;
		RBTree_Add( &fd_by_name_index, &ret->name_index_node, &name );
		return ret;
	}
	return NULL;
}

fd_t * fd_iter_next(fd_t *current, const char *from_name) {
	RBTreeNode *node;
	if (current) {
		node= RBTreeNode_GetNext(&current->name_index_node);
	} else {
		RBTreeSearch s= RBTree_Find( &fd_by_name_index, from_name );
		if (s.Nearest == NULL)
			node= NULL;
		else if (s.Relation > 0)
			node= s.Nearest;
		else
			node= RBTreeNode_GetNext(s.Nearest);
	}
	return node? (fd_t *) node->Object : NULL;
}

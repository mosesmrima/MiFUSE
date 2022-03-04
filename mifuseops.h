#ifndef MIFUSE_MIFUSEOPS_H
#define MIFUSE_MIFUSEOPS_H
#ifdef __FreeBSD__
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include <sys/file.h> /* flock(2) */
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>
/* Include if you have both fuse version 2 and 3 installed
 * to explicitly specify which version 2 use so as to avoid
 * confusion while compiling
 */
#include <fuse3/fuse.h>


static void *mi_init(struct fuse_conn_info *conn,
		     struct fuse_config *cfg);

static int mi_getattr(const char *path, struct stat *st,
		      struct fuse_file_info *fi);

static int mi_access(const char *path, int mask);
static int mi_readlink(const char *path, char *buf, size_t size);
static int mi_opendir(const char *path, struct fuse_file_info *fi);

static int mi_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		      off_t offset, struct fuse_file_info *fi,
		      enum fuse_readdir_flags flags);

static int mi_releasedir(const char *path, struct fuse_file_info *fi);
static int mi_mknod(const char *path, mode_t mode, dev_t dev);
static int mi_mkdir(const char *path, mode_t mode);
static int mi_unlink(const char *path);
static int mi_rmdir(const char *path);
static int mi_symlink(const char *path, const char *link);

static int mi_rename(const char *path, const char *newpath,
		     unsigned int flags);

static int mi_link(const char *path, const char *newpath);
static int mi_chmod(const char*path, mode_t mode, struct fuse_file_info *fi);

static int mi_chown(const char *path,uid_t uid, gid_t gid,
		    struct fuse_file_info *fi);

static int mi_truncate(const char *path, off_t newsize,
		       struct fuse_file_info *fi);

#ifdef HAVE_UTIMENSAT
static int mi_utimens(const char *path, const struct timespec ts[2],
                      struct fuse_file_info *fi);
#endif

static int mi_create(const char *path, mode_t mode, struct fuse_file_info *fi);
static int mi_open(const char *path, struct fuse_file_info *fi);

static int mi_read(__attribute__((unused)) const char *path, char *buf, size_t size,
		   off_t offset,struct fuse_file_info *fi);

static int mi_read_buf(const char *path, struct fuse_bufvec **bufp,
		       size_t size, off_t offset, struct fuse_file_info *fi);

static int mi_write(const char *path, const char *buf,
		    size_t size, off_t offset, struct fuse_file_info *fi);

static int mi_write_buf(const char *path, struct fuse_bufvec *buf,
			off_t offset, struct fuse_file_info *fi);

static int mi_statfs(const char *path, struct statvfs *statv);
static int mi_flush(const char *path, struct fuse_file_info *fi);
static int mi_release(const char *path, struct fuse_file_info *fi);

static int mi_fsync(const char *path, int isdatasync,
		    struct fuse_file_info *fi);

static int mi_setxattr(const char *path, const char *name, const char *value,
		       size_t size, int flags);

static int mi_getxattr(const char *path, const char *name, char *value,
		       size_t size);

static int mi_listxattr(const char *path, char *list, size_t size);
static int mi_removexattr(const char *path, const char *name);


static int mi_flock(const char *path, struct fuse_file_info *fi, int op);

ssize_t mi_copy_file_range(const char *path_in,
			   struct fuse_file_info *fi_in,
			   off_t off_in, const char *path_out,
			   struct fuse_file_info *fi_out,
			   off_t off_out, size_t len, int flags);

static off_t mi_lseek(const char *path, off_t off,int whence,
		      struct fuse_file_info *fi);

#endif //MIFUSE_MIFUSEOPS_H
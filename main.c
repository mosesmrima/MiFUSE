#define _GNU_SOURCE


/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700

/**
 * FUSE API version broke backward compatibility as of fuse 3.0
 * hence we need to specify which API version we assume
 */
#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <fcntl.h>

#include <stdio.h>
#include <sys/xattr.h>

#include "params.h"
#include "mifuseops.h"
/* Include this if you have both fuse2 and fuse3 installed */
#include <fuse3/fuse.h>
#include <sys/wait.h>


/**
 * mi_init - initializes the filesystem. The return value will be passed to the
 *           private_data field of fuse_context to all file operations.
 * @conn: connection information of the filesystem
 * @cfg: configuration info of the high level FUSE API
 * Return: always MI_DATA
 */
static void *mi_init(struct fuse_conn_info *conn,
    struct fuse_config *cfg)
{

    (void) conn;
    /* honor the st_ino fields in getattr/filldir*/
    cfg->use_ino = 1;
    /*
     * prevent file-system operations such as rea, write,readdir
     * from receiving path information.
     * for operations such as chmod, chown and utimens, path
     * info will be provided only if the struct fuse_file_info
     * argument is null
     */
    cfg->nullpath_ok = 1; //

    /*
     * Pick up changes from lower filesystem right away. This is
     *  also necessary for better hardlink support. When the kernel
     * calls the unlink() handler, it does not know the inode of
     * the to-be-removed entry and can therefore not invalidate
     * the cache of the associated inode - resulting in an
     * incorrect st_nlink value being reported for any remaining
     * hardlinks to this inode.
     * the ..._timeout(s)represent the timeout in seconds for which the
     * respective values will be cached
     * setting the values to 0 ensures that hte values are not cached
     */
    /* don't cache name lookups*/
    cfg->entry_timeout = 0;
    /* don't cache file/dir attributes as returned by getattr*/
    cfg->attr_timeout = 0;
    /*
     * don't cache negative look ups i.e when getattr returns ENOENT meaning
     * the file doesnt exist
     */
    cfg->negative_timeout = 0;
    /*
     * get the current context of the filesystem
     * fuse_get_context()->private_data returns the user_data passed to
     * fuse_main()
     */
    fuse_get_context();

    return MI_DATA;
}

/**
 *mi_fullpath - all paths ins fuse are relative to the root f the mounted
 *              filesystem. This function constructs the actual absolute
 *              path in the underlying filesystem
 * @fpath: absolute path of a fil/dir in the underlying filesystem
 * @path: path relative to the root of the mounted filesystem
 * Return: this function does not return
 */
static void mi_fullpath(char fpath[PATH_MAX], const char *path)
{
    strcpy(fpath, MI_DATA->root_dir);
    strncat(fpath, path, PATH_MAX);
}

/**
 * mi_getattr - get file attributes
 *              Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 *              ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 *              mount option is given.
 * @path: filepath relative to the root of the filesystem
 * @st: struct stat to store the retrieved file attributes
 * Return: 0 on sauces, -ve errnor on failure
 */
static int mi_getattr(const char *path, struct stat *st,
                      struct fuse_file_info *fi)
{
    int ret;

    char fpath[PATH_MAX];
    mi_fullpath(fpath, path);

    if(fi)
        ret = fstat(fi->fh, st);
    else
        ret = lstat(fpath, st);
    if (ret == -1)
        return -errno;

    return ret;
}

/**
 * mi_access - check whether the calling process can access the file specified
 *             by path
 * @path: path of the file to check access permissions
 * @mask:the accessibility check to be performed
 * Return: 0 on success, otherwise -ve errno
 */
static int mi_access(const char *path, int mask)
{
    int rs;
    char fpath[PATH_MAX];

    mi_fullpath(fpath, path);

    rs = access(fpath, mask);

    if (rs < 0)
        return -errno;
    return rs;
}

/**
 * mi_readlink - rea the contents of a symbolic link
 * @path: symbolic link path
 * @buf: stores the read contents
 * @size: number of characters to read plus one(for null terminator)
 * Return: on success, the number of bytes read is returned
 *         otherwise -ve errno is returned
 */
static int mi_readlink(const char *path, char *buf, size_t size)
{
    ssize_t rs;
    char fpath[PATH_MAX];

    mi_fullpath(fpath, path);

    rs = readlink(fpath, buf, size - 1);
    if (rs == -1)
        return -errno;
    /*null terminate the read string*/
    buf[rs] = '\0';
    return 0;
}

/**
 * struct mi_drp - hold directory entry information
 * @dp: pointer to directory stream
 * @entry: holds the information of the current directory entry
 * @offset: offset of the directory entry
 * Description - this struct holds all the information about a single directory
 *                entry.
 */
struct mi_dirp
{
    DIR *dp;
    struct dirent *entry;
    off_t offset;
};

/**
 * mi_opendir - open a directory
 * @path: path of directory to open
 * @fi: holds information of the opened directory
 * Return: 0 on success, -ve errno otherwise
 */
static int mi_opendir(const char *path, struct fuse_file_info *fi)
{
    int rs;
    char fpath[PATH_MAX];
    struct mi_dirp *d = malloc(sizeof(struct mi_dirp));
    if (d == NULL)
        return -ENOMEM;

    mi_fullpath(fpath, path);

    d->dp = opendir(fpath);
    if (d->dp == NULL) {
        rs = -errno;
        return rs;
    }
    /* set offset to zero to signify the first directory entry */
    d->offset = 0;
    /* set to null to signify no entry has een read yet */
    d->entry = NULL;

    /* set fie handler to the returned directory stream */
    fi->fh = (unsigned long) d;
    return 0;
}

/**
 * get_dirp - get file handler of a directory entry
 * @fi: information of the opened directory
 * @Return: file handler id of the open directory cast as a pointer to mi_dirp
 */
static inline struct mi_dirp *get_dirp(struct fuse_file_info *fi)
{
    return (struct mi_dirp *) (uintptr_t) fi->fh;
}

/**
 * mi_readdir - read the contents of a directory
 * @path: path of the directory to be read, we do not need to use path to get
 *        the directory stream pointer since we can easily get it from get_dirp
 * @buf: buffer to be filled with information of the directory entries
 * @filler: pointer to a function that adds every read entry into buf
 * @offset: offset of the net entry or zero
 * @fi: hods information of the open directory
 * Return: zero always
 */
static int mi_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags)
{
    (void) path;

    struct mi_dirp *d = get_dirp(fi);

    if (offset != d->offset) {
#ifndef __FreeBSD__
        seekdir(d->dp, offset);
#else
        /* Subtract the one that we add when calling telldir() below */
		seekdir(d->dp, offset-1);
#endif
        d->entry = NULL;
        d->offset = offset;
    }
    while (1) {
        struct stat st;
        off_t nextoff;
        enum fuse_fill_dir_flags fill_flags = 0;

        if (!d->entry) {
            d->entry = readdir(d->dp);
            if (!d->entry)
                break;
        }
#ifdef HAVE_FSTATAT
        if (flags & FUSE_READDIR_PLUS) {
			int res;

			res = fstatat(dirfd(d->dp), d->entry->d_name, &st,
				      AT_SYMLINK_NOFOLLOW);
			if (res != -1)
				fill_flags |= FUSE_FILL_DIR_PLUS;
		}
#endif
        if (!(fill_flags & FUSE_FILL_DIR_PLUS)) {
            memset(&st, 0, sizeof(st));
            st.st_ino = d->entry->d_ino;
            st.st_mode = d->entry->d_type << 12;
        }
        nextoff = telldir(d->dp);
#ifdef __FreeBSD__
        /* Under FreeBSD, telldir() may return 0 the first time
         * it is called. But for libfuse, an offset of zero
         * means that offsets are not supported, so we shift
         * everything by one.
         */
		nextoff++;
#endif
        if (filler(buf, d->entry->d_name, &st, nextoff, fill_flags))
            break;

        d->entry = NULL;
        d->offset = nextoff;
    }

    return 0;
}

/**
 * mi_releasedir - closes an open directory stream
 * @path: path of the directory to be closed, we do not need it seance we can
 * easily get the stream id  from get_dirp.
 * @fi: information aout the opendir
 * Return: always zero
 */
static int mi_releasedir(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    struct mi_dirp *d = get_dirp(fi);
    closedir(d->dp);

    return 0;
}

/**
 * mi_mknod - creates a file system node (file or device special file or pipe)
 * @path: path to the node to be created
 * @mode: specifies the mode and type of the node
 * @dev: specifies the device id where the node is to be created
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_mknod(const char *path, mode_t mode, dev_t dev)
{
    int rs;
    char fpath[PATH_MAX];

    mi_fullpath(fpath, path);

    /* On Linux this could just be 'mknod(path, mode, dev)' but this
     * tries to be be more portable by honoring the quote in the Linux
     * mknod man page stating the only portable use of mknod() is to
     * make a fifo, but saying it should never actually be used for
     * that.
     */

    /* Checks if it's a regular file */
    if (S_ISREG(mode)) {
        rs = open(fpath, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (rs >= 0)
            rs = close(rs);
    }
    else
        /* Checks if it's a pipe */
    if (S_ISFIFO(mode))
        rs = mkfifo(fpath, mode);
    else
        rs = mknod(fpath, mode, dev);

    if (rs == -1)
        return -errno;
    return 0;
}


/**
 * mi_mkdir - create a directory
 * @path: the path of the directory to create
 * @mode: specifies the mode/ properties of the directory to be created
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_mkdir(const char *path, mode_t mode)
{
    int rs;
    char fpath[PATH_MAX];

    mi_fullpath(fpath, path);

    rs = mkdir(fpath, mode);
    if (rs == -1)
        return -errno;
    return rs;
}

/**
 * mi_unlink - remove/delete a filename or symbolic link from the filesystem
 * @path: path to the file to remove
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_unlink(const char *path)
{
    int rs;
    char fpath[PATH_MAX];

    mi_fullpath(fpath, path);

    rs = unlink(fpath);
    if (rs < 0)
        return -errno;

    return rs;
}

/**
 * mi_rmdir - delete a directory from the filesystem
 * @path: path to the directory to delete
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_rmdir(const char *path)
{
    int rs;
    char fpath[PATH_MAX];

    mi_fullpath(fpath, path);

    rs = rmdir(fpath);
    if (rs < 0)
        return -errno;
    return rs;
}

/**
 * mi_symlink - create a symbolic link
 * @path: where the symbolic link should point to
 * @link: the link to create
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_symlink(const char *path, const char *link)
{
    char flink[PATH_MAX];
    int rs;

    mi_fullpath(flink, link);

    rs = symlink(path, flink);
    if (rs == -1)
        return -errno;
    return rs;
}

/**
 * mi_rename - rename a file, moving it btw directories if required
 * @path: path to the file to rename
 * @newpath: new name of the file
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_rename(const char *path, const char *newpath,
                     unsigned int flags)
{
    char fpath[PATH_MAX];
    char fnew_path[PATH_MAX];
    int rs;

    mi_fullpath(fpath, path);
    mi_fullpath(fnew_path, newpath);

    /* When we have renameat2() in libc, then we can implement flags */
    if (flags)
        return -EINVAL;

    rs = rename(fpath, fnew_path);
    if (rs == -1)
        return -errno;
    return rs;
}
/**
 * mi_link - create a hardlink to a file
 * @path: name of the file you want to create a a hardlink for
 * @newpath: name of the hardlink
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_link(const char *path, const char *newpath)
{
    char fpath[PATH_MAX];
    char fnew_path[PATH_MAX];
    int rs;

    mi_fullpath(fpath, path);
    mi_fullpath(fnew_path, newpath);

    rs = link(fpath, fnew_path);

    if (rs == -1)
        return -errno;
    return rs;
}

/**
 * mi_chmod - change mode bits of a file
 * @path: name of the file t change mode bits
 * @mode: new mode bits of the file
 * @fi: open file descriptor to the file
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_chmod(const char*path, mode_t mode, struct fuse_file_info *fi)
{
    char fpath[PATH_MAX];
    int rs;

    mi_fullpath(fpath, path);

    if (fi)
        rs = fchmod(fi->fh, mode);
    else
        rs = chmod(fpath, mode);

    if (rs == -1)
        return -errno;
    return rs;
}

/**
 * mi_chown - change owner and group of a file
 * @path: name of the file
 * @uid: the user id of the file
 * @gid: the group id of the file
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_chown(const char *path,uid_t uid, gid_t gid,
                    struct fuse_file_info *fi)
{
    char fpath[PATH_MAX];
    int rs;

    mi_fullpath(fpath, path);

    if(fi)
        rs = fchown(fi->fh, uid, gid);
    else
        rs = lchown(fpath, uid, gid);

    if (rs == -1)
        return -errno;
    return rs;
}
/**
 * mi_truncate - change the size of a file
 * @path: name of he file
 * @newsize: the new size of the file
 * @fi: open file descriptor
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_truncate(const char *path, off_t newsize,
                       struct fuse_file_info *fi)
{
    char fpath[PATH_MAX];
    int rs;

    mi_fullpath(fpath, path);
    if(fi)
        rs = ftruncate(fi->fh, newsize);
    else
        rs = truncate(fpath, newsize);

    if (rs == -1)
        return -errno;
    return rs;
}


/**
 * mi_utimens - change the access and/ modification time of a file
 * @path: name of the path
 * @ts:
 * @fi:
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_utimens(const char *path, const struct timespec ts[2],
        struct fuse_file_info *fi)
{
    int rs;
    char fpath[PATH_MAX];
    (void) fi;

    mi_fullpath(fpath, path);

    if (fi)
        rs = futimens(fi->fh, ts);
    else
        rs = utimensat(0, fpath, ts, AT_SYMLINK_NOFOLLOW);

    if (rs == -1)
        return -errno;
    return rs;
}



/**
 * mi_create - create and open a file, create it if it does not exist
 * @path: name of the file
 * @mode: mode bits of the file(permissions/uid/gid/sticky bits)
 * @fi: file handler
 * Return - 0 on success, -ve errno on failure
 */
static int mi_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    char fpath[PATH_MAX];
    int fd;

    mi_fullpath(fpath, path);

    fd = open(fpath, fi->flags, mode);
    if (fd == -1)
        return -errno;

    fi->fh = fd;
    return 0;
}

/**
 * mi_open - open an existing file
 * @path: the name of the file
 * @fi: file handler
 * Return - open file descriptor on success, -ve errno otherwise
 */
static int mi_open(const char *path, struct fuse_file_info *fi)
{
    int fd;
    char fpath[PATH_MAX];
    mi_fullpath(fpath, path);
    fd = open(fpath, fi->flags);
    if (fd == -1)
        return  -errno;
    fi->fh = fd;
    return fd;
}

/**
 *  mi_read - read from a file descriptor from an offset upto a a given number
 *            of bytes into a buffer
 * @path: name/path of the file, we do not need it since we are using pread and
 *        can get the file descriptor from fi.
 * @buf: buffer to hold the read data
 * @size: number of bytes to read
 * @offset: offset from where to start reading
 * @fi: the open file handler
 * Return - number of bytes read on success, -ve errno otherwise
 */
static int mi_read(__attribute__((unused)) const char *path, char *buf, size_t size,
                   off_t offset,struct fuse_file_info *fi)
{
    int rs;
    (void) path;

    rs = pread(fi->fh, buf, size, offset);
    if (rs == -1)
        return -errno;
    return rs;
}

/**
 * mi_read_buf - Store data from an open file in a buffer. Similar to the
 *               mi_read() method, but data is stored and returned in a
 *               generic buffer. No actual copying of data has to take place,
 *               the source file descriptor may simply be stored in the buffer
 *               for later data transfer.
 * @path: name/path of the file, we do not need it since we have the file
 *        descriptor.
 * @bufp: pointer to te buffer where data or e file descriptor is copied into
 * @size: number of bytes to read
 * @offset: offset int he file from where we want to read
 * @fi: file handler
 * Return - always zero
 */
static int mi_read_buf(const char *path, struct fuse_bufvec **bufp,
                       size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct fuse_bufvec *src;

    (void) path;

    src = malloc(sizeof(struct fuse_bufvec));
    if (src == NULL)
        return -ENOMEM;

    *src = FUSE_BUFVEC_INIT(size);

    src->buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
    src->buf[0].fd = fi->fh;
    src->buf[0].pos = offset;

    *bufp = src;

    return 0;
}

/**
 * mi_write - writes up to count bytes from the buffer starting at buf to the
 *            file descriptor fd at  offset  offset.
 * @path: path/name of the file to write t, we do not need it since pwrite uses
 *        file descriptor fi->fh
 * @buf: buffer containing the data to be written
 * @size: number of bytes to be written
 * @offset: offset in the file where writing should begin
 * @fi: file handler to be used to get the file descriptor
 * Return -number of bytes written on success, -ve errno otherwise
 */
static int mi_write(const char *path, const char *buf,
                    size_t size, off_t offset, struct fuse_file_info *fi)
{
    int ret;
    char fpath[PATH_MAX];
    mi_fullpath(fpath, path);
    ret = pwrite(fi->fh, buf, size, offset);
    if (ret < 0)
        return -errno;
    return ret;
}

/**
 * mi_write_buf - Write contents of buffer to an open file. Similar to the
 *                mi_write() method, but data is supplied in a generic buffer.
 *                Use fuse_buf_copy() to transfer data to the destination.
 * @path: path/name of the file, not needed since we have the file handler
 * @buf: buffer containing the data to be written
 * @offset: offset in the file where the writing should begin
 * @fi: the file handler
 * Return - on success, number of bytes written, -ve errno otherwise
 */
static int mi_write_buf(const char *path, struct fuse_bufvec *buf,
                        off_t offset, struct fuse_file_info *fi)
{
    struct fuse_bufvec dst = FUSE_BUFVEC_INIT(fuse_buf_size(buf));

    (void) path;

    dst.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
    dst.buf[0].fd = fi->fh;
    dst.buf[0].pos = offset;

    return fuse_buf_copy(&dst, buf, FUSE_BUF_SPLICE_NONBLOCK);
}

/**
 * mi_statfs - get information abut a mounted file system
 * @path: pathname of a file within the filesystem
 * @statv: pointer to a statvfs structure that holds the returned information
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_statfs(const char *path, struct statvfs *statv)
{
    int rs;
    char fpath[PATH_MAX];
    mi_fullpath(fpath, path);

    /* Get stats for underlying filesystem */
    rs = statvfs(fpath, statv);

    if(rs == -1)
        return -errno;
    return rs;
}

/**
 * mi_flush - Possibly flush cached data.
 * @path: pathname of the file, not needed here since file descriptor is used
 * @fi: file handler
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_flush(const char *path, struct fuse_file_info *fi)
{
    int rs;
    (void) path;

    /*This is called from every close on an open file, so call the
     * close on the underlying filesystem.	But since flush may be
     * called multiple times for an open file, this must not really
     * close the file.  This is important if used on a network
     * filesystem like NFS which flush the data/metadata on close() */
    rs = close(dup(fi->fh));
    if (rs == -1)
        return -errno;
    return 0;
}

/**
 * mi_release - called when there are no more references to an open file:
 *              all file descriptors are closed and all memory mappings are
 *              unmapped.
 * @path: pathname of the file
 * @fi: file handler
 * Return - 0 on success, -ve errno on failure
 */
static int mi_release(const char *path, struct fuse_file_info *fi)
{
    int ret;
    (void) path;
    ret = close(fi->fh);
    if (ret < 0)
        return -errno;
    return ret;
}

/**
 * mi_fsync - transfers ("flushes") all modified in-core data of
 *             (i.e., modified buffer cache pages for) the file re‐ferred to by
 *             the file descriptor fd to the disk device (or other permanent
 *             storage device) so that all  changed information  can  be
 *             retrieved  even  if  the system crashes or is rebooted.
 * @path: pathname of the file, not used since we have the file descriptor
 * @isdatasync: If the datasync parameter is non-zero, then only the user
 *              data should be flushed, not the meta data.
 * @fi: the file handler
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_fsync(const char *path, int isdatasync,
                    struct fuse_file_info *fi)
{
    int res;
    (void) path;

#ifndef HAVE_FDATASYNC
    (void) isdatasync;
#else
    if (isdatasync)
		res = fdatasync(fi->fh);
	else
#endif
    res = fsync(fi->fh);
    if (res == -1)
        return -errno;

    return 0;
}




/**
 * mi_setattr - set extended attributes of a file
 * @path: pathname of the file
 * @name: name of the attribute
 * @value: the value of the attribute
 * @size: size of the attribute
 * @flags: any flags
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_setxattr(const char *path, const char *name, const char *value,
                        size_t size, int flags)
{
    char fpath[PATH_MAX];
    int res;

    mi_fullpath(fpath, path);

    res = lsetxattr(fpath, name, value, size, flags);
    if (res == -1)
        return -errno;
    return 0;
}

/**
 * mi_getxattr - retrieve the extended attribute value associated to @name
 *               of files and symbolic links.
 * @path: pathname of the file
 * @name: name of the extended attribute
 * @value: the value of the named extended attribute pointed to by buf
 * @size: size of the extended attribute buffer pointed to by name
 * Return - the size of the extended attribute on success, -ve errno on success
 */
static int mi_getxattr(const char *path, const char *name, char *value,
                        size_t size)
{
    int res;
    char fpath[PATH_MAX];

    mi_fullpath(fpath, path);

    res = lgetxattr(fpath, name, value, size);
    if (res == -1)
        return -errno;
    return res;
}

/**
 * mi_getxattr - retrieves  the  list of extended attribute names associated
 *               with the given path in the filesystem
 * @path: pathname of the file
 * @list:  a list of null terminated names of the extended attribute
 *         associated with the file
 * @size: size of the list of extended attributes
 * Return - the length of the list extended attributes on success,
 *          -ve errno on success
 */
static int mi_listxattr(const char *path, char *list, size_t size)
{
    int res;
    char fpath[PATH_MAX];

    mi_fullpath(fpath, path);

    res = llistxattr(fpath, list, size);
    if (res == -1)
        return -errno;
    return res;
}

/**
 * mi_removeattr - removes  the  extended  attribute  identified  by @name and
 *                 associated with the given @path in the filesystem.
 * @path: pathname of the file or a symbolic link.
 * @name: name of the extended attribute to be removed
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_removexattr(const char *path, const char *name)
{
    int res;
    char fpath[PATH_MAX];

    mi_fullpath(fpath, path);

    res = lremovexattr(fpath, name);
    if (res == -1)
        return -errno;
    return 0;
}


#ifdef HAVE_POSIX_FALLOCATE
/**
 * mi_fallocate - Allocates space for an open file. This function ensures that
 *                required space is allocated for specified file. If this
 *                function returns success then any subsequent write request to
 *                specified range is guaranteed not to fail because of lack of
 *                space on the file system media.
 * @path: pathname of the file
 * @mode: mode bits
 * @offset: offset in the file where writing should start
 * @length: number of bytes to be allocated
 * @fi: file handler
 * Return - 0 on success, -EOPNOTSUPP if mode bits are set, -ve errno otherwise
 */
static int mi_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	(void) path;

	if (mode)
		return -EOPNOTSUPP;

	return -posix_fallocate(fi->fh, offset, length);
}
#endif



#ifdef HAVE_LIBULOCKMGR
/**
 * mi_lock - Perform POSIX file locking operation.
 * @path: pathname of the file to be locked
 * @fi: file handler
 * @cmd: **
 * @lock: a pointer to the system call flock()
 * Return - 0 on success, -ve errno otherwise
 */
int mi_lock(const char *path, struct fuse_file_info *fi, int cmd,
                    struct flock *lock)
{
    (void) path;

    return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
                       sizeof(fi->lock_owner));
}
#endif

/**
 * mi_flock - Apply  or  remove an advisory lock on the open file specified by
 *            @fi->fh.
 * @path: pathname of the file
 * @fi: file handler
 * @op : maybe one of:
 *                   LOCK_SH  Place a shared lock.  More than one process may
 *                   hold a shared lock for a given  file  at  a  given time.
                     LOCK_EX  Place  an exclusive lock.  Only one process may
                     hold an exclusive lock for a given file at a given
                     time.

                      LOCK_UN  Remove an existing lock held by this process.
 * Return - 0 on success, -ve errno otherwise
 */
static int mi_flock(const char *path, struct fuse_file_info *fi, int op)
{
    int res;
    (void) path;

    res = flock(fi->fh, op);
    if (res == -1)
        return -errno;

    return 0;
}

#ifdef HAVE_COPY_FILE_RANGE
/**
 * mi_copy_file_range - Copy a range of data from one file to another.
 *                      Performs an optimized copy between two file descriptors
 *                      without the additional cost of transferring data through
 *                      the FUSE kernel module to user space (glibc) and then
 *                      back into the FUSE filesystem again
 * @path_in: pathname of the file to copy from
 * @fi_in: file handler of the file to copy from
 * @off_in: offset of in the file where copying from should begin
 * @path_out: pathname of the file to copy to
 * @fi_out: file handler of the file to copy to
 * @off_out: offset in the file where copying should begin
 * @len: the number of bytes to be transferred
 * @flags: **
 * Return - number f bytes transferred on success, -1 otherwise
 */
ssize_t mi_copy_file_range(const char *path_in,
                                   struct fuse_file_info *fi_in,
                                   off_t off_in, const char *path_out,
                                   struct fuse_file_info *fi_out,
                                   off_t off_out, size_t len, int flags)
{
    ssize_t res;
    (void) path_in;
    (void) path_out;

    res = copy_file_range(fi_in->fh, &off_in, fi_out->fh, &off_out, len,
                          flags);
    if (res == -1)
        return -errno;

    return res;
}
#endif

/**
 * mi_lseek - repositions the file offset of the open file description
 *            associated with the file @fi->fh descriptor to the argument offset
 *            according to the directive @whence
 * @path: pathname of the file
 * @off: offset of repositioning
 * @whence: direction to seek
 * @fi: open file handler
 * Return - 0 on success, -ve errno otherwise
 */
static off_t mi_lseek(const char *path, off_t off,int whence,
                      struct fuse_file_info *fi)
{
    off_t res;
    (void) path;

    res = lseek(fi->fh, off, whence);
    if (res == -1)
        return -errno;

    return res;
}



/**
 * struct fuse_operations mi_ops - a structure that stores all MiFUSE file
 *         operations.
 */
static const struct fuse_operations mi_ops = {
        .init = mi_init,
        .getattr = mi_getattr,
        .access = mi_access,
        .readlink = mi_readlink,
        .readdir = mi_readdir,
        .opendir = mi_opendir,
        .releasedir = mi_releasedir,
        .mknod = mi_mknod,
        .mkdir = mi_mkdir,
        .unlink = mi_unlink,
        .rmdir = mi_rmdir,
        .symlink = mi_symlink,
        .rename = mi_rename,
        .link = mi_link,
        .chmod = mi_chmod,
        .chown = mi_chown,
        .truncate = mi_truncate,
        .utimens	= mi_utimens,
        .create = mi_create,
        .open = mi_open,
        .read = mi_read,
        .read_buf = mi_read_buf,
        .write = mi_write,
        .write_buf = mi_write_buf,
        .statfs = mi_statfs,
        .flush = mi_flush,
        .release = mi_release,
        .fsync  = mi_fsync,

#ifdef HAVE_POSIX_ALLOCATE
        .fallocate = mi_fallocate
#endif


        .setxattr	= mi_setxattr,
        .getxattr	= mi_getxattr,
	    .listxattr	= mi_listxattr,
	    .removexattr	= mi_removexattr,


#ifdef HAVE_LIBULOCKMGR
        .lock		= mi_lock,
#endif
        .flock		= mi_flock,
#ifdef HAVE_COPY_FILE_RANGE
        .copy_file_range = mi_copy_file_range,
#endif
        .lseek		= mi_lseek,

};



/**
 * mifuse_usage - prints out a message to to show how to run mifuse
 */
void mifuse_usage()
{
    fprintf(stderr,
            "usage:  ./mfuse [FUSE and mount options]"
            " rootDir mountPoint\n");
    abort();
}

/**
 * main - main function
 * @argc: commandline argument count
 * @argv: an array of commandline arguments
 * Return: 0 on success, 1 on error
 */
int main(int argc, char *argv[]) {
	pid_t pid = fork();
	if (pid == 0)
	{
		if (access("root.zip", F_OK) == 0)
		{
			system("unzip -oj root.zip");
		}
	}
	else
	{
		sleep(5);
		wait(NULL);
		struct mi_state *mi_data;
		int ret_stat;

		/* restrict root from mounting the filesystem to prevent isk of privilege
		 * escalation
		 */
		if ((getuid() == 0) || (geteuid() == 0))
		{
			fprintf(stderr,
				"Running MiFUSE as root opens unacceptable"
				" security holes\n");

			return 1;
		}
		/* Perform sanity checking on the commandline arguments passed:
		 *      1. enough arguments
		 *      2. FUSE and mount options come last, then mountain and lastly root
		 *          of the filesystem
		 */
		if ((argc < 3) || (argv[argc - 2][0] == '-')
		    || (argv[argc - 1][0] == '-'))
			mifuse_usage();
		fprintf(stderr, "Fuse library version %d.%d\n",
			FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);

		mi_data = malloc(sizeof(struct mi_state));
		if (mi_data == NULL)
		{
			perror("main calloc");
			abort();
		}
		/* pull the rootdir from the cmd arguments and save it */
		mi_data->root_dir = realpath(argv[argc - 2], NULL);
		argv[argc - 2] = argv[argc - 1];
		argv[argc - 1] = NULL;
		argc--;
		/* pass over control to fuse */
		ret_stat = fuse_main(argc, argv, &mi_ops, mi_data);
		pid = fork();
		if (pid == 0) {

				sleep(5);
				system("zip -j /home/mrima/Downloads/mifuse-main/root /home/mrima/Downloads/mifuse-main/root/*");
		}
		else {

		}
		return ret_stat;
	}

}

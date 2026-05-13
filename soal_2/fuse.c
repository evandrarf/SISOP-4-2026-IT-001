#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#define FUSE_USE_VERSION 31

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

static const char *ENC_SUFFIX = ".enc";
static const size_t ENC_SUFFIX_LEN = 4;
static const unsigned char XOR_KEY = 0x76;

struct moo_state {
    char source_dir[PATH_MAX];
};

static struct moo_state *moo_get_state(void)
{
    return (struct moo_state *) fuse_get_context()->private_data;
}

static bool has_enc_suffix(const char *name)
{
    size_t len = strlen(name);
    return len >= ENC_SUFFIX_LEN && strcmp(name + len - ENC_SUFFIX_LEN, ENC_SUFFIX) == 0;
}

static int build_source_path(char *dest, size_t dest_size, const char *path, bool encrypted_file)
{
    const char *source_dir = moo_get_state()->source_dir;
    int written;

    if (strcmp(path, "/") == 0) {
        written = snprintf(dest, dest_size, "%s", source_dir);
    } else if (encrypted_file) {
        written = snprintf(dest, dest_size, "%s%s%s", source_dir, path, ENC_SUFFIX);
    } else {
        written = snprintf(dest, dest_size, "%s%s", source_dir, path);
    }

    if (written < 0 || (size_t) written >= dest_size) {
        return -ENAMETOOLONG;
    }

    return 0;
}

static int resolve_existing_path(const char *path, char *resolved, size_t resolved_size, struct stat *stbuf)
{
    int status = build_source_path(resolved, resolved_size, path, false);
    if (status != 0) {
        return status;
    }

    if (lstat(resolved, stbuf) == 0) {
        return 0;
    }

    int dir_errno = errno;

    status = build_source_path(resolved, resolved_size, path, true);
    if (status != 0) {
        return status;
    }

    if (lstat(resolved, stbuf) == 0) {
        return 0;
    }

    return -(errno == ENOENT ? dir_errno : errno);
}

static void xor_buffer(char *buf, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        buf[i] ^= XOR_KEY;
    }
}

static int moo_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void) fi;
    memset(stbuf, 0, sizeof(*stbuf));

    char resolved[PATH_MAX];
    int status = resolve_existing_path(path, resolved, sizeof(resolved), stbuf);
    if (status != 0) {
        return status;
    }

    return 0;
}

static int moo_access(const char *path, int mask)
{
    char resolved[PATH_MAX];
    struct stat stbuf;
    int status = resolve_existing_path(path, resolved, sizeof(resolved), &stbuf);
    if (status != 0) {
        return status;
    }

    if (access(resolved, mask) == -1) {
        return -errno;
    }

    return 0;
}

static int moo_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi,
                       enum fuse_readdir_flags flags)
{
    (void) offset;
    (void) fi;
    (void) flags;

    char resolved[PATH_MAX];
    int status = build_source_path(resolved, sizeof(resolved), path, false);
    if (status != 0) {
        return status;
    }

    DIR *dir = opendir(resolved);
    if (dir == NULL) {
        return -errno;
    }

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char display_name[NAME_MAX + 1];
        memset(display_name, 0, sizeof(display_name));

        if (entry->d_type == DT_DIR || !has_enc_suffix(entry->d_name)) {
            snprintf(display_name, sizeof(display_name), "%s", entry->d_name);
        } else {
            size_t len = strlen(entry->d_name) - ENC_SUFFIX_LEN;
            memcpy(display_name, entry->d_name, len);
            display_name[len] = '\0';
        }

        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = entry->d_ino;
        st.st_mode = entry->d_type << 12;

        if (filler(buf, display_name, &st, 0, 0) != 0) {
            break;
        }
    }

    closedir(dir);
    return 0;
}

static int moo_mkdir(const char *path, mode_t mode)
{
    char resolved[PATH_MAX];
    int status = build_source_path(resolved, sizeof(resolved), path, false);
    if (status != 0) {
        return status;
    }

    if (mkdir(resolved, mode) == -1) {
        return -errno;
    }

    return 0;
}

static int moo_rmdir(const char *path)
{
    char resolved[PATH_MAX];
    int status = build_source_path(resolved, sizeof(resolved), path, false);
    if (status != 0) {
        return status;
    }

    if (rmdir(resolved) == -1) {
        return -errno;
    }

    return 0;
}

static int moo_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    char resolved[PATH_MAX];
    int status = build_source_path(resolved, sizeof(resolved), path, true);
    if (status != 0) {
        return status;
    }

    int fd = open(resolved, fi->flags | O_CREAT, mode);
    if (fd == -1) {
        return -errno;
    }

    fi->fh = (uint64_t) fd;
    return 0;
}

static int moo_open(const char *path, struct fuse_file_info *fi)
{
    char resolved[PATH_MAX];
    int status = build_source_path(resolved, sizeof(resolved), path, true);
    if (status != 0) {
        return status;
    }

    int fd = open(resolved, fi->flags);
    if (fd == -1) {
        return -errno;
    }

    fi->fh = (uint64_t) fd;
    return 0;
}

static int moo_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    (void) path;

    ssize_t bytes_read = pread((int) fi->fh, buf, size, offset);
    if (bytes_read == -1) {
        return -errno;
    }

    xor_buffer(buf, (size_t) bytes_read);
    return (int) bytes_read;
}

static int moo_write(const char *path, const char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    (void) path;

    char *encrypted = malloc(size);
    if (encrypted == NULL) {
        return -ENOMEM;
    }

    memcpy(encrypted, buf, size);
    xor_buffer(encrypted, size);

    ssize_t bytes_written = pwrite((int) fi->fh, encrypted, size, offset);
    free(encrypted);

    if (bytes_written == -1) {
        return -errno;
    }

    return (int) bytes_written;
}

static int moo_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    if (fi != NULL) {
        if (ftruncate((int) fi->fh, size) == -1) {
            return -errno;
        }

        return 0;
    }

    char resolved[PATH_MAX];
    int status = build_source_path(resolved, sizeof(resolved), path, true);
    if (status != 0) {
        return status;
    }

    if (truncate(resolved, size) == -1) {
        return -errno;
    }

    return 0;
}

static int moo_unlink(const char *path)
{
    char resolved[PATH_MAX];
    int status = build_source_path(resolved, sizeof(resolved), path, true);
    if (status != 0) {
        return status;
    }

    if (unlink(resolved) == -1) {
        return -errno;
    }

    return 0;
}

static int moo_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi)
{
    (void) fi;

    char resolved[PATH_MAX];
    struct stat stbuf;
    int status = resolve_existing_path(path, resolved, sizeof(resolved), &stbuf);
    if (status != 0) {
        return status;
    }

    if (utimensat(AT_FDCWD, resolved, tv, AT_SYMLINK_NOFOLLOW) == -1) {
        return -errno;
    }

    return 0;
}

static int moo_release(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    close((int) fi->fh);
    fi->fh = 0;
    return 0;
}

static const struct fuse_operations moo_ops = {
    .getattr = moo_getattr,
    .readdir = moo_readdir,
    .mkdir = moo_mkdir,
    .rmdir = moo_rmdir,
    .create = moo_create,
    .open = moo_open,
    .read = moo_read,
    .write = moo_write,
    .truncate = moo_truncate,
    .unlink = moo_unlink,
    .access = moo_access,
    .utimens = moo_utimens,
    .release = moo_release,
};

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <encrypted_storage> <mount_point> [FUSE options]\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct stat st;
    if (stat(argv[1], &st) == -1 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Source directory is invalid: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    struct moo_state *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    if (realpath(argv[1], state->source_dir) == NULL) {
        perror("realpath");
        free(state);
        return EXIT_FAILURE;
    }

    int fuse_argc = argc - 1;
    char **fuse_argv = calloc((size_t) fuse_argc + 1, sizeof(char *));
    if (fuse_argv == NULL) {
        perror("calloc");
        free(state);
        return EXIT_FAILURE;
    }

    fuse_argv[0] = argv[0];
    for (int i = 2; i < argc; i++) {
        fuse_argv[i - 1] = argv[i];
    }
    fuse_argv[fuse_argc] = NULL;

    int status = fuse_main(fuse_argc, fuse_argv, &moo_ops, state);

    free(fuse_argv);
    free(state);
    return status;
}

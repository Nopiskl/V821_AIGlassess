#ifndef V821_MEDIA_OWNER_H
#define V821_MEDIA_OWNER_H

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* Shared by the local MPP runtime and all controlled-mode application entries.
 * Keep the descriptor open for the complete runtime; never unlink the lock. */
static inline int v821_owner_acquire(const char *path)
{
    struct stat st;
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &st) < 0) goto fail;
    if (!S_ISREG(st.st_mode) || st.st_uid != geteuid()) { errno = EACCES; goto fail; }
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) goto fail;
    return fd;
fail: {
    int saved = errno;
    close(fd);
    errno = saved;
    return -1;
}
}
static inline int v821_media_owner_acquire(void)
{
    return v821_owner_acquire("/tmp/v821-media-owner.lock");
}
static inline int v821_mode_owner_acquire(void)
{
    return v821_owner_acquire("/tmp/v821-mode-owner.lock");
}
#endif

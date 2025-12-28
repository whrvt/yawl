/*
 * nsenter(1) - command-line interface for setns(2)
 *
 * Source from the https://github.com/util-linux/util-linux repository,
 * adapted for use internally with yawl.
 *
 * Copyright (C) 2012-2023 Eric Biederman <ebiederm@xmission.com>
 * Copyright (C) 2025 William Horvath
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * See the full license text in the repository LICENSE file.
 */
/*
 * Constant strings for usage() functions. For more info see
 * Documentation/{howto-usage-function.txt,boilerplate.c}
 */

#include "config.h"

#include <cassert>
#include <dirent.h>
#include <err.h>
#include <cerrno>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <cinttypes>
#include <libintl.h>
#include <climits>
#include <clocale>
#include <sched.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctime>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#ifndef NS_GET_USERNS
#define NS_GET_USERNS _IO(0xb7, 0x1)
#endif

#define SIOCGSKNS 0x894C

#ifndef PR_CAP_AMBIENT
#define PR_CAP_AMBIENT 47
#define PR_CAP_AMBIENT_IS_SET 1
#define PR_CAP_AMBIENT_RAISE 2
#define PR_CAP_AMBIENT_LOWER 3
#endif

#define _PATH_PROC_CAPLASTCAP "/proc/sys/kernel/cap_last_cap"

#define _PATH_SYS_CGROUP "/sys/fs/cgroup"
#define STATFS_CGROUP2_MAGIC 0x63677270
#define STATFS_PROC_MAGIC 0x9fa0

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/share/locale"
#endif

#define F_TYPE_EQUAL(a, b) (a == (__typeof__(a))b)

#define stringify_value(s) stringify(s)
#define stringify(s) #s

#define strtoul_or_err(_s, _e) (unsigned long)str2unum_or_err(_s, 10, _e)

#define UL_EXCL_STATUS_INIT {0}

/*
 * pidfd ioctls
 *
 * All added by commit to kernel 6.11, commit 5b08bd408534bfb3a7cf5778da5b27d4e4fffe12.
 */
#ifdef HAVE_SYS_PIDFD_H
#include <sys/pidfd.h>
#endif

#ifndef PIDFS_IOCTL_MAGIC
#define PIDFS_IOCTL_MAGIC 0xFF
#define PIDFD_GET_CGROUP_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 1)
#define PIDFD_GET_IPC_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 2)
#define PIDFD_GET_MNT_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 3)
#define PIDFD_GET_NET_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 4)
#define PIDFD_GET_PID_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 5)
#define PIDFD_GET_PID_FOR_CHILDREN_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 6)
#define PIDFD_GET_TIME_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 7)
#define PIDFD_GET_TIME_FOR_CHILDREN_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 8)
#define PIDFD_GET_USER_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 9)
#define PIDFD_GET_UTS_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 10)
#endif

#if !defined(HAVE_PIDFD_OPEN) && defined(SYS_pidfd_open)
static inline int pidfd_open(pid_t pid, unsigned int flags) { return syscall(SYS_pidfd_open, pid, flags); }
#endif

#if !defined(HAVE_PIDFD_GETFD) && defined(SYS_pidfd_getfd)
static inline int pidfd_getfd(int pidfd, int targetfd, unsigned int flags) {
    return syscall(SYS_pidfd_getfd, pidfd, targetfd, flags);
}
#endif

/*
 * Dummy fallbacks for cases when #ifdef HAVE_PIDFD_* makes the code too complex.
 */

#if !defined(HAVE_PIDFD_SEND_SIGNAL) && !defined(SYS_pidfd_send_signal)
static inline int pidfd_send_signal(int pidfd __attribute__((unused)), int sig __attribute__((unused)),
                                    siginfo_t *info __attribute__((unused)),
                                    unsigned int flags __attribute__((unused))) {
    errno = ENOSYS;
    return -1;
}
#endif

#if !defined(HAVE_PIDFD_OPEN) && !defined(SYS_pidfd_open)
static inline int pidfd_open(pid_t pid __attribute__((unused)), unsigned int flags __attribute__((unused))) {
    errno = ENOSYS;
    return -1;
}
#endif

#if !defined(HAVE_PIDFD_GETFD) && !defined(SYS_pidfd_getfd)
static inline int pidfd_getfd(int pidfd __attribute__((unused)), int targetfd __attribute__((unused)),
                              unsigned int flags __attribute__((unused))) {
    errno = ENOSYS;
    return -1;
}
#endif

/* yawl-specific defines */

#include "capability.h"
#include "log.hpp"
#include "nsenter.hpp"
#include "macros.hpp"

#define LOG_ERROR_AND_RETURN(...)                                                                                      \
    do {                                                                                                               \
        LOG_ERROR("nsenter: %s", __VA_ARGS__);                                                                         \
        return;                                                                                                        \
    } while (0)

#define LOG_ERROR_RET_MINUSONE(...)                                                                                    \
    do {                                                                                                               \
        LOG_ERROR("nsenter: %s", __VA_ARGS__);                                                                         \
        return -1;                                                                                                     \
    } while (0)

#define LOG_ERROR_RET_ZERO(...)                                                                                        \
    do {                                                                                                               \
        LOG_ERROR("nsenter: %s", __VA_ARGS__);                                                                         \
        return 0;                                                                                                      \
    } while (0)

/* end yawl-specific defines */

static struct timespec smallwaittime = {.tv_sec = 250000 / 1000000L, .tv_nsec = (250000 % 1000000L) * 1000};

typedef int ul_excl_t[16];

static struct namespace_file {
    int nstype;
    const char *name;
    int fd;
    bool enabled;
} namespace_files[] = {
    /* Careful the order is significant in this array.
     *
     * The user namespace comes either first or last: first if
     * you're using it to increase your privilege and last if
     * you're using it to decrease.  We enter the namespaces in
     * two passes starting initially from offset 1 and then offset
     * 0 if that fails.
     */
    {.nstype = CLONE_NEWUSER, .name = "ns/user", .fd = -1, .enabled = false},
    {.nstype = CLONE_NEWCGROUP, .name = "ns/cgroup", .fd = -1, .enabled = false},
    {.nstype = CLONE_NEWIPC, .name = "ns/ipc", .fd = -1, .enabled = false},
    {.nstype = CLONE_NEWUTS, .name = "ns/uts", .fd = -1, .enabled = false},
    {.nstype = CLONE_NEWNET, .name = "ns/net", .fd = -1, .enabled = false},
    {.nstype = CLONE_NEWPID, .name = "ns/pid", .fd = -1, .enabled = false},
    {.nstype = CLONE_NEWNS, .name = "ns/mnt", .fd = -1, .enabled = false},
    {.nstype = CLONE_NEWTIME, .name = "ns/time", .fd = -1, .enabled = false},
    {.nstype = 0, .name = NULL, .fd = -1, .enabled = false}};

#define USAGE_HEADER "\nUsage:\n"
#define USAGE_OPTIONS "\nOptions:\n"
#define USAGE_FUNCTIONS "\nFunctions:\n"
#define USAGE_COMMANDS "\nCommands:\n"
#define USAGE_ARGUMENTS "\nArguments:\n"
#define USAGE_COLUMNS "\nAvailable output columns:\n"
#define USAGE_DEFAULT_COLUMNS "\nDefault columns:\n"
#define USAGE_SEPARATOR "\n"

#define USAGE_OPTSTR_HELP "display this help"

#define USAGE_HELP_OPTIONS(marg_dsc) "%-" #marg_dsc "s%s\n", " -h, --help", USAGE_OPTSTR_HELP

static void __attribute__((__noreturn__)) usage(void) {
    FILE *out = stdout;

    fputs(USAGE_HEADER, out);
    fprintf(out, " nsenter [options] [<program> [<argument>...]]\n");

    fputs(USAGE_SEPARATOR, out);
    fputs("Run a program with namespaces of other processes.\n", out);

    fputs(USAGE_OPTIONS, out);
    fputs(" -a, --all              enter all namespaces\n", out);
    fputs(" -t, --target <pid>     target process to get namespaces from\n", out);
    fputs(" -m, --mount[=<file>]   enter mount namespace\n", out);
    fputs(" -u, --uts[=<file>]     enter UTS namespace (hostname etc)\n", out);
    fputs(" -i, --ipc[=<file>]     enter System V IPC namespace\n", out);
    fputs(" -n, --net[=<file>]     enter network namespace\n", out);
    fputs(" -N, --net-socket <fd>  enter socket's network namespace (use with --target)\n", out);
    fputs(" -p, --pid[=<file>]     enter pid namespace\n", out);
    fputs(" -C, --cgroup[=<file>]  enter cgroup namespace\n", out);
    fputs(" -U, --user[=<file>]    enter user namespace\n", out);
    fputs("     --user-parent      enter parent user namespace\n", out);
    fputs(" -T, --time[=<file>]    enter time namespace\n", out);
    fputs(" -S, --setuid[=<uid>]   set uid in entered namespace\n", out);
    fputs(" -G, --setgid[=<gid>]   set gid in entered namespace\n", out);
    fputs("     --preserve-credentials do not touch uids or gids\n", out);
    fputs("     --keep-caps        retain capabilities granted in user namespaces\n", out);
    fputs(" -r, --root[=<dir>]     set the root directory\n", out);
    fputs(" -w, --wd[=<dir>]       set the working directory\n", out);
    fputs(" -W, --wdns <dir>       set the working directory in namespace\n", out);
    fputs(" -e, --env              inherit environment variables from target process\n", out);
    fputs(" -F, --no-fork          do not fork before exec'ing <program>\n", out);
    fputs(" -c, --join-cgroup      join the cgroup of the target process\n", out);

    fputs(USAGE_SEPARATOR, out);
    fprintf(out, USAGE_HELP_OPTIONS(24));

    exit(EXIT_SUCCESS);
}

static pid_t namespace_target_pid = 0;
static int root_fd = -1;
static int wd_fd = -1;
static int env_fd = -1;
static int uid_gid_fd = -1;
static int cgroup_procs_fd = -1;

static inline struct namespace_file *__next_nsfile(struct namespace_file *n, int namespaces, bool enabled) {
    if (!n)
        n = namespace_files;
    else if (n->nstype != 0)
        n++;

    for (; n && n->nstype; n++) {
        if (namespaces && !(n->nstype & namespaces))
            continue;
        if (enabled && !n->enabled)
            continue;
        return n;
    }

    return NULL;
}

#define next_nsfile(_n, _ns) __next_nsfile(_n, _ns, 0)
#define next_enabled_nsfile(_n, _ns) __next_nsfile(_n, _ns, 1)

#define get_nsfile(_ns) __next_nsfile(NULL, _ns, 0)
#define get_enabled_nsfile(_ns) __next_nsfile(NULL, _ns, 1)

static void open_target_fd(int *fd, const char *type, const char *path) {
    char pathbuf[PATH_MAX];

    if (!path && namespace_target_pid) {
        snprintf(pathbuf, sizeof(pathbuf), "/proc/%u/%s", namespace_target_pid, type);
        path = pathbuf;
    }
    if (!path)
        errx(EXIT_FAILURE, "neither filename nor target pid supplied for %s", type);

    if (*fd >= 0)
        close(*fd);

    *fd = open(path, O_RDONLY);
    if (*fd < 0)
        err(EXIT_FAILURE, "cannot open %s", path);
}

static void enable_nsfile(struct namespace_file *n, const char *path) {
    if (path)
        open_target_fd(&n->fd, n->name, path);
    n->enabled = true;
}

static void disable_nsfile(struct namespace_file *n) {
    if (n->fd >= 0)
        close(n->fd);
    n->fd = -1;
    n->enabled = false;
}

/* Enable namespace; optionally open @path if not NULL. */
static void enable_namespace(int nstype, const char *path) {
    struct namespace_file *nsfile = get_nsfile(nstype);

    if (nsfile)
        enable_nsfile(nsfile, path);
    else
        assert(nsfile);
}

static void disable_namespaces(int namespaces) {
    struct namespace_file *n = NULL;

    while ((n = next_enabled_nsfile(n, namespaces)))
        disable_nsfile(n);
}

/* Returns mask of all enabled namespaces */
static int get_namespaces(void) {
    struct namespace_file *n = NULL;
    int mask = 0;

    while ((n = next_enabled_nsfile(n, 0)))
        mask |= n->nstype;
    return mask;
}

static int get_namespaces_without_fd(void) {
    struct namespace_file *n = NULL;
    int mask = 0;

    while ((n = next_enabled_nsfile(n, 0))) {
        if (n->fd < 0)
            mask |= n->nstype;
    }

    return mask;
}

/* Open /proc/#/ns/ files for enabled namespaces specified in @namespaces
 * if they have not been opened yet. */
static void open_namespaces(int namespaces) {
    struct namespace_file *n = NULL;

    while ((n = next_enabled_nsfile(n, namespaces))) {
        if (n->fd < 0)
            open_target_fd(&n->fd, n->name, NULL);
    }
}

static int do_setns(int fd, int ns, const char *name, bool ignore_errors) {
    int rc = setns(fd, ns);

    if (rc < 0 && !ignore_errors) {
        if (name)
            err(EXIT_FAILURE, "reassociate to namespace '%s' failed", name);
        else
            err(EXIT_FAILURE, "reassociate to namespaces failed");
    }
    return rc;
}

static void enter_namespaces(int pid_fd, int namespaces, bool ignore_errors) {
    struct namespace_file *n = NULL;

    if (pid_fd) {
        int ns = 0;
        while ((n = next_enabled_nsfile(n, namespaces))) {
            if (n->fd < 0)
                ns |= n->nstype;
        }
        if (ns && do_setns(pid_fd, ns, NULL, ignore_errors) == 0)
            disable_namespaces(ns);
    }

    n = NULL;
    while ((n = next_enabled_nsfile(n, namespaces))) {
        if (n->fd < 0)
            continue;
        if (do_setns(n->fd, n->nstype, n->name, ignore_errors) == 0)
            disable_nsfile(n);
    }
}

static void open_parent_user_ns_fd(int pid_fd) {
    struct namespace_file *user = NULL;
    int fd = -1, parent_fd = -1;
    bool islocal = false;

    /* try user namespace if FD defined */
    user = get_nsfile(CLONE_NEWUSER);
    if (user->enabled)
        fd = user->fd;

    /* try pidfd to get FD */
    if (fd < 0 && pid_fd >= 0) {
        fd = ioctl(pid_fd, PIDFD_GET_USER_NAMESPACE, 0);
        if (fd >= 0)
            islocal = true;
    }

    /* try any enabled namespace */
    if (fd < 0) {
        struct namespace_file *n = get_enabled_nsfile(0);
        if (n)
            fd = n->fd;
    }

    /* try directly open the NS */
    if (fd < 0) {
        open_target_fd(&fd, "ns/user", NULL);
        islocal = true;
    }

    parent_fd = ioctl(fd, NS_GET_USERNS);
    if (parent_fd < 0)
        err(EXIT_FAILURE, "failed to open parent namespace");

    if (islocal)
        close(fd);
    if (user->fd > 0)
        close(user->fd);
    user->fd = parent_fd;
    user->enabled = true;
}

static void open_target_sk_netns(int pidfd, int sock_fd) {
    struct namespace_file *nsfile;
    struct stat sb;
    int sk, nsfd;
    bool local_fd = false;

    nsfile = get_nsfile(CLONE_NEWNET);
    assert(nsfile->nstype);

    if (pidfd < 0) {
        pidfd = pidfd_open(namespace_target_pid, 0);
        if (pidfd < 0)
            err(EXIT_FAILURE, "failed to pidfd_open() for %d", namespace_target_pid);
        local_fd = true;
    }

    sk = pidfd_getfd(pidfd, sock_fd, 0);
    if (sk < 0)
        err(EXIT_FAILURE, "pidfd_getfd(%d, %u)", pidfd, sock_fd);

    if (fstat(sk, &sb) < 0)
        err(EXIT_FAILURE, "fstat(%d)", sk);

    nsfd = ioctl(sk, SIOCGSKNS);
    if (nsfd < 0)
        err(EXIT_FAILURE, "ioctl(%d, SIOCGSKNS)", sk);

    if (nsfile->fd >= 0)
        close(nsfile->fd);
    nsfile->fd = nsfd;
    nsfile->enabled = true;
    close(sk);

    if (local_fd)
        close(pidfd);
}

static int get_ns_ino(const char *path, ino_t *ino) {
    struct stat st;

    if (stat(path, &st) != 0)
        return -errno;
    *ino = st.st_ino;
    return 0;
}

static inline int write_all(int fd, const void *buf, size_t count) {
    while (count) {
        ssize_t tmp;

        errno = 0;
        tmp = write(fd, buf, count);
        if (tmp > 0) {
            count -= tmp;
            if (count)
                buf = (const void *)((const char *)buf + tmp);
        } else if (errno != EINTR && errno != EAGAIN)
            return -1;
        if (errno == EAGAIN) /* Try later, *sigh* */
            nanosleep(&smallwaittime, NULL);
    }
    return 0;
}

static inline ssize_t read_all(int fd, char *buf, size_t count) {
    ssize_t ret;
    ssize_t c = 0;
    int tries = 0;

    memset(buf, 0, count);
    while (count > 0) {
        ret = read(fd, buf, count);
        if (ret < 0) {
            if ((errno == EAGAIN || errno == EINTR) && (tries++ < 5)) {
                nanosleep(&smallwaittime, NULL);
                continue;
            }
            return c ? c : -1;
        }
        if (ret == 0)
            return c;
        tries = 0;
        count -= ret;
        buf += ret;
        c += ret;
    }
    return c;
}

static inline ssize_t read_all_alloc(int fd, char **buf) {
    size_t size = 1024, c = 0;
    ssize_t ret;

    *buf = (char *)malloc(size);
    if (!*buf)
        return -1;

    while (1) {
        ret = read_all(fd, *buf + c, size - c);
        if (ret < 0) {
            free(*buf);
            *buf = NULL;
            return -1;
        }

        if (ret == 0)
            return c;

        c += ret;
        if (c == size) {
            size *= 2;
            *buf = (char *)realloc(*buf, size);
            if (!*buf)
                return -1;
        }
    }
}

static void open_cgroup_procs(void) {
    char *buf = NULL, *path = NULL, *p;
    int cgroup_fd = 0;
    char fdpath[PATH_MAX];

    open_target_fd(&cgroup_fd, "cgroup", optarg);

    if (read_all_alloc(cgroup_fd, &buf) < 1)
        err(EXIT_FAILURE, "failed to get cgroup path");

    p = strtok(buf, "\n");
    if (p)
        path = strrchr(p, ':');
    if (!path)
        err(EXIT_FAILURE, "failed to get cgroup path");
    path++;

    snprintf(fdpath, sizeof(fdpath), _PATH_SYS_CGROUP "/%s/cgroup.procs", path);

    if ((cgroup_procs_fd = open(fdpath, O_WRONLY | O_APPEND)) < 0)
        err(EXIT_FAILURE, "failed to open cgroup.procs");

    free(buf);
}

static int is_cgroup2(void) {
    struct statfs fs_stat;
    int rc;

    rc = statfs(_PATH_SYS_CGROUP, &fs_stat);
    if (rc) {
        LOG_ERROR("statfs %s failed", _PATH_SYS_CGROUP);
        return 0;
    }
    return F_TYPE_EQUAL(fs_stat.f_type, STATFS_CGROUP2_MAGIC);
}

static void join_into_cgroup(void) {
    pid_t pid;
    char buf[sizeof(stringify_value(UINT32_MAX))];
    int len;

    pid = getpid();
    len = snprintf(buf, sizeof(buf), "%zu", (size_t)pid);
    if (write_all(cgroup_procs_fd, buf, len))
        err(EXIT_FAILURE, "write cgroup.procs failed");
}

static int is_usable_namespace(pid_t target, const struct namespace_file *nsfile) {
    char path[PATH_MAX];
    ino_t my_ino = 0;
    int rc;

    /* Check NS accessibility */
    snprintf(path, sizeof(path), "/proc/%u/%s", getpid(), nsfile->name);
    rc = get_ns_ino(path, &my_ino);
    if (rc == -ENOENT)
        return false; /* Unsupported NS */

    /* It is not permitted to use setns(2) to reenter the caller's
     * current user namespace; see setns(2) man page for more details.
     */
    if (nsfile->nstype & CLONE_NEWUSER) {
        ino_t target_ino = 0;

        snprintf(path, sizeof(path), "/proc/%u/%s", target, nsfile->name);
        if (get_ns_ino(path, &target_ino) != 0)
            err(EXIT_FAILURE, "stat of %s failed", path);

        if (my_ino == target_ino)
            return false;
    }

    return true; /* All pass */
}

static void continue_as_child(void) {
    pid_t child;
    int status;
    pid_t ret;

    /* Clear any inherited settings */
    signal(SIGCHLD, SIG_DFL);

    child = fork();
    if (child < 0)
        err(EXIT_FAILURE, "fork failed");

    /* Only the child returns */
    if (child == 0)
        return;

    for (;;) {
        ret = waitpid(child, &status, WUNTRACED);
        if ((ret == child) && (WIFSTOPPED(status))) {
            /* The child suspended so suspend us as well */
            kill(getpid(), SIGSTOP);
            kill(child, SIGCONT);
        } else {
            break;
        }
    }
    /* Return the child's exit code if possible */
    if (WIFEXITED(status)) {
        exit(WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        kill(getpid(), WTERMSIG(status));
    }
    exit(EXIT_FAILURE);
}

/* Storage for command-line options. */
#define MAX_SPEED 10 /* max. nr. of baud rates */

typedef unsigned int speed_t;

struct options {
    int flags;                 /* toggle switches, see below */
    unsigned int timeout;      /* time-out period */
    char *autolog;             /* login the user automatically */
    char *chdir;               /* Chdir before the login */
    char *chroot;              /* Chroot before the login */
    char *login;               /* login program */
    char *logopt;              /* options for login program */
    const char *tty;           /* name of tty */
    const char *vcline;        /* line of virtual console */
    char *term;                /* terminal type */
    char *initstring;          /* modem init string */
    char *issue;               /* alternative issue file or directory */
    char *erasechars;          /* string with erase chars */
    char *killchars;           /* string with kill chars */
    char *osrelease;           /* /etc/os-release data */
    unsigned int delay;        /* Sleep seconds before prompt */
    int nice;                  /* Run login with this priority */
    int numspeed;              /* number of baud rates to try */
    int clocal;                /* CLOCAL_MODE_* */
    int kbmode;                /* Keyboard mode if virtual console */
    int tty_is_stdin;          /* is the tty the standard input stream */
    speed_t speeds[MAX_SPEED]; /* baud rates to be tried */
};

static inline const char *option_to_longopt(int c, const struct option *opts) {
    const struct option *o;

    assert(!(opts == NULL));
    for (o = opts; o->name; o++)
        if (o->val == c)
            return o->name;
    return NULL;
}

/* Cases for lowercase hex letters, and lowercase letters, all offset by N. */
#define _C_CTYPE_LOWER_A_THRU_F_N(N)                                                                                   \
    case 'a' + (N):                                                                                                    \
    case 'b' + (N):                                                                                                    \
    case 'c' + (N):                                                                                                    \
    case 'd' + (N):                                                                                                    \
    case 'e' + (N):                                                                                                    \
    case 'f' + (N)
#define _C_CTYPE_LOWER_N(N)                                                                                            \
    _C_CTYPE_LOWER_A_THRU_F_N(N) : case 'g' + (N):                                                                     \
    case 'h' + (N):                                                                                                    \
    case 'i' + (N):                                                                                                    \
    case 'j' + (N):                                                                                                    \
    case 'k' + (N):                                                                                                    \
    case 'l' + (N):                                                                                                    \
    case 'm' + (N):                                                                                                    \
    case 'n' + (N):                                                                                                    \
    case 'o' + (N):                                                                                                    \
    case 'p' + (N):                                                                                                    \
    case 'q' + (N):                                                                                                    \
    case 'r' + (N):                                                                                                    \
    case 's' + (N):                                                                                                    \
    case 't' + (N):                                                                                                    \
    case 'u' + (N):                                                                                                    \
    case 'v' + (N):                                                                                                    \
    case 'w' + (N):                                                                                                    \
    case 'x' + (N):                                                                                                    \
    case 'y' + (N):                                                                                                    \
    case 'z' + (N)

/* Cases for hex letters, digits, lower, punct, and upper. */
#define _C_CTYPE_A_THRU_F _C_CTYPE_LOWER_A_THRU_F_N(0) : _C_CTYPE_LOWER_A_THRU_F_N('A' - 'a')
#define _C_CTYPE_DIGIT                                                                                                 \
    case '0':                                                                                                          \
    case '1':                                                                                                          \
    case '2':                                                                                                          \
    case '3':                                                                                                          \
    case '4':                                                                                                          \
    case '5':                                                                                                          \
    case '6':                                                                                                          \
    case '7':                                                                                                          \
    case '8':                                                                                                          \
    case '9'
#define _C_CTYPE_LOWER _C_CTYPE_LOWER_N(0)
#define _C_CTYPE_PUNCT                                                                                                 \
    case '!':                                                                                                          \
    case '"':                                                                                                          \
    case '#':                                                                                                          \
    case '$':                                                                                                          \
    case '%':                                                                                                          \
    case '&':                                                                                                          \
    case '\'':                                                                                                         \
    case '(':                                                                                                          \
    case ')':                                                                                                          \
    case '*':                                                                                                          \
    case '+':                                                                                                          \
    case ',':                                                                                                          \
    case '-':                                                                                                          \
    case '.':                                                                                                          \
    case '/':                                                                                                          \
    case ':':                                                                                                          \
    case ';':                                                                                                          \
    case '<':                                                                                                          \
    case '=':                                                                                                          \
    case '>':                                                                                                          \
    case '?':                                                                                                          \
    case '@':                                                                                                          \
    case '[':                                                                                                          \
    case '\\':                                                                                                         \
    case ']':                                                                                                          \
    case '^':                                                                                                          \
    case '_':                                                                                                          \
    case '`':                                                                                                          \
    case '{':                                                                                                          \
    case '|':                                                                                                          \
    case '}':                                                                                                          \
    case '~'
#define _C_CTYPE_UPPER _C_CTYPE_LOWER_N('A' - 'a')

static inline int c_isgraph(int c) {
    switch (c) {
    _C_CTYPE_DIGIT:
    _C_CTYPE_LOWER:
    _C_CTYPE_PUNCT:
    _C_CTYPE_UPPER:
        return 1;
    default:
        return 0;
    }
}

static inline void err_exclusive_options(int c, const struct option *opts, const ul_excl_t *excl, int *status) {
    int e;

    for (e = 0; excl[e][0] && excl[e][0] <= c; e++) {
        const int *op = excl[e];

        for (; *op && *op <= c; op++) {
            if (*op != c)
                continue;
            if (status[e] == 0)
                status[e] = c;
            else if (status[e] != c) {
                size_t ct = 0;

                LOG_ERROR("nsenter: mutually exclusive arguments:");

                for (op = excl[e]; ct + 1 < ARRAY_SIZE(excl[0]) && *op; op++, ct++) {
                    const char *n = option_to_longopt(*op, opts);
                    if (n)
                        fprintf(stderr, " --%s", n);
                    else if (c_isgraph(*op))
                        fprintf(stderr, " -%c", *op);
                }
                fputc('\n', stderr);
                exit(EXIT_FAILURE);
            }
            break;
        }
    }
}

static int ul_strtou64(const char *str, uint64_t *num, int base) {
    char *end = NULL;
    int64_t tmp;

    if (str == NULL || *str == '\0')
        return -(errno = EINVAL);

    /* we need to ignore negative numbers, note that for invalid negative
     * number strtoimax() returns negative number too, so we do not
     * need to check errno here */
    errno = 0;
    tmp = (int64_t)strtoimax(str, &end, base);
    if (tmp < 0)
        errno = ERANGE;
    else {
        errno = 0;
        *num = strtoumax(str, &end, base);
    }

    if (errno != 0)
        return -errno;
    if (str == end || (end && *end))
        return -(errno = EINVAL);
    return 0;
}

static int ul_strtos64(const char *str, int64_t *num, int base) {
    char *end = NULL;

    if (str == NULL || *str == '\0')
        return -(errno = EINVAL);

    errno = 0;
    *num = (int64_t)strtoimax(str, &end, base);

    if (errno != 0)
        return -errno;
    if (str == end || (end && *end))
        return -(errno = EINVAL);
    return 0;
}

static int64_t str2num_or_err(const char *str, int base, const char *errmesg, int64_t low, int64_t up) {
    int64_t num = 0;
    int rc;

    rc = ul_strtos64(str, &num, base);
    if (rc == 0 && ((low && num < low) || (up && num > up)))
        rc = -(errno = ERANGE);

    if (rc) {
        if (errno == ERANGE)
            err(EXIT_FAILURE, "%s: '%s'", errmesg, str);
        errx(EXIT_FAILURE, "%s: '%s'", errmesg, str);
    }
    return num;
}

unsigned long str2unum(const char *str, int base) {
    unsigned long num = 0;
    int rc;

    rc = ul_strtou64(str, &num, base);
    if (rc == 0 && (ULONG_MAX && num > ULONG_MAX))
        rc = -(errno = ERANGE);

    if (rc)
        return 0;

    return num;
}

static uint64_t str2unum_or_err(const char *str, int base, const char *errmesg) {
    uint64_t num = 0;

    if (!(num = str2unum(str, base))) {
        if (errno == ERANGE)
            err(EXIT_FAILURE, "%s: '%s'", errmesg, str);
        errx(EXIT_FAILURE, "%s: '%s'", errmesg, str);
    }

    return num;
}

struct ul_env_list {
    char *name;
    char *value;

    struct ul_env_list *next;
};

static struct ul_env_list *env_list_add(struct ul_env_list *ls0, const char *name, size_t namesz, const char *value,
                                        size_t valsz) {
    struct ul_env_list *ls;

    ls = (struct ul_env_list *)calloc(1, sizeof(struct ul_env_list) + namesz + valsz + 2);
    if (!ls)
        return ls0;

    ls->name = ((char *)ls) + sizeof(struct ul_env_list);
    ls->value = ls->name + namesz + 1;

    memcpy(ls->name, name, namesz);
    memcpy(ls->value, value, valsz);

    ls->next = ls0;
    return ls;
}

/*
 * Saves the @str (with the name=value string) to the @ls and returns a pointer
 * to the new head of the list.
 */
static struct ul_env_list *env_list_add_from_string(struct ul_env_list *ls, const char *str) {
    size_t namesz = 0, valsz = 0;
    const char *val;

    if (!str || !*str)
        return ls;

    val = strchr(str, '=');
    if (!val)
        return NULL;
    namesz = val - str;

    val++;
    valsz = strlen(val);

    return env_list_add(ls, str, namesz, val, valsz);
}

/*
 * Use env_list_from_fd() to read environment from @fd.
 *
 * @fd must be /proc/<pid>/environ file.
 */
static struct ul_env_list *env_list_from_fd(int fd) {
    char *buf = NULL, *p;
    ssize_t rc = 0;
    struct ul_env_list *ls = NULL;

    errno = 0;
    if ((rc = read_all_alloc(fd, &buf)) < 1)
        return NULL;
    buf[rc] = '\0';
    p = buf;

    while (rc > 0) {
        ls = env_list_add_from_string(ls, p);
        p += strlen(p) + 1;
        rc -= strlen(p) + 1;
    }

    free(buf);
    return ls;
}

/*
 * Use setenv() for all stuff in @ls.
 */
static int env_list_setenv(struct ul_env_list *ls, int overwrite) {
    int rc = 0;

    while (ls && rc == 0) {
        if (ls->name && ls->value)
            rc = setenv(ls->name, ls->value, overwrite);
        ls = ls->next;
    }
    return rc;
}

static void env_list_free(struct ul_env_list *ls) {
    while (ls) {
        struct ul_env_list *x = ls;
        ls = ls->next;
        free(x);
    }
}

static int test_cap(unsigned int cap) {
    /* prctl returns 0 or 1 for valid caps, -1 otherwise */
    return prctl(PR_CAPBSET_READ, cap, 0, 0, 0) >= 0;
}

static int cap_last_by_bsearch(int *ret) {
    /* starting with cap=INT_MAX means we always know
     * that cap1 is invalid after the first iteration */
    int cap = INT_MAX;
    unsigned int cap0 = 0, cap1 = INT_MAX;

    while ((int)cap0 < cap) {
        if (test_cap(cap))
            cap0 = cap;
        else
            cap1 = cap;

        cap = (cap0 + cap1) / 2U;
    }

    *ret = cap;
    return 0;
}

#ifdef HAVE_SYS_VFS_H
/* checks if fd is file in a procfs;
 * returns 1 if true, 0 if false or couldn't determine */
static int fd_is_procfs(int fd) {
    struct statfs st;
    int ret;

    do {
        errno = 0;
        ret = fstatfs(fd, &st);

        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN)
                return 0;
            nanosleep(&smallwaittime, NULL);
        }
    } while (ret != 0);

    return st.f_type == STATFS_PROC_MAGIC;
    return 0;
}
#else
static int fd_is_procfs(int fd __attribute__((__unused__))) { return 0; }
#endif

static int cap_last_by_procfs(int *ret) {
    FILE *f = fopen(_PATH_PROC_CAPLASTCAP, "r");
    int rc = -EINVAL;

    *ret = 0;

    if (f && fd_is_procfs(fileno(f))) {
        int cap;

        /* we check if the cap after this one really isn't valid */
        if (fscanf(f, "%d", &cap) == 1 && cap < INT_MAX && !test_cap(cap + 1)) {

            *ret = cap;
            rc = 0;
        }
    }

    if (f)
        fclose(f);
    return rc;
}

static int cap_last_cap(void) {
    static int cap = -1;

    if (cap != -1)
        return cap;
    if (cap_last_by_procfs(&cap) < 0)
        cap_last_by_bsearch(&cap);

    return cap;
}

static void cap_permitted_to_ambient(void) {
    /* We use capabilities system calls to propagate the permitted
     * capabilities into the ambient set because we may have
     * already forked so be in async-signal-safe context. */
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct payload[_LINUX_CAPABILITY_U32S_3] = {};
    uint64_t effective, cap;

    if (capget(&header, payload) < 0)
        err(EXIT_FAILURE, "capget failed");

    /* In order the make capabilities ambient, we first need to ensure
     * that they are all inheritable. */
    payload[0].inheritable = payload[0].permitted;
    payload[1].inheritable = payload[1].permitted;

    if (capset(&header, payload) < 0)
        err(EXIT_FAILURE, "capset failed");

    effective = ((uint64_t)payload[1].effective << 32) | (uint64_t)payload[0].effective;

    for (cap = 0; cap < (sizeof(effective) * 8); cap++) {
        /* This is the same check as cap_valid(), but using
         * the runtime value for the last valid cap. */
        if (cap > (uint64_t)cap_last_cap())
            continue;

        if ((effective & (1ULL << cap)) && prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap, 0, 0) < 0)
            err(EXIT_FAILURE, "prctl(PR_CAP_AMBIENT) failed");
    }
}

int do_nsenter(int argc, char *argv[], unsigned long pid_to_enter) {
    enum {
        OPT_PRESERVE_CRED = CHAR_MAX + 1,
        OPT_KEEPCAPS,
        OPT_USER_PARENT,
    };
    static const struct option longopts[] = {{"all", no_argument, NULL, 'a'},
                                             {"help", no_argument, NULL, 'h'},
                                             {"version", no_argument, NULL, 'V'},
                                             {"target", required_argument, NULL, 't'},
                                             {"mount", optional_argument, NULL, 'm'},
                                             {"uts", optional_argument, NULL, 'u'},
                                             {"ipc", optional_argument, NULL, 'i'},
                                             {"net", optional_argument, NULL, 'n'},
                                             {"net-socket", required_argument, NULL, 'N'},
                                             {"pid", optional_argument, NULL, 'p'},
                                             {"user", optional_argument, NULL, 'U'},
                                             {"cgroup", optional_argument, NULL, 'C'},
                                             {"time", optional_argument, NULL, 'T'},
                                             {"setuid", required_argument, NULL, 'S'},
                                             {"setgid", required_argument, NULL, 'G'},
                                             {"root", optional_argument, NULL, 'r'},
                                             {"wd", optional_argument, NULL, 'w'},
                                             {"wdns", optional_argument, NULL, 'W'},
                                             {"env", no_argument, NULL, 'e'},
                                             {"no-fork", no_argument, NULL, 'F'},
                                             {"join-cgroup", no_argument, NULL, 'c'},
                                             {"preserve-credentials", no_argument, NULL, OPT_PRESERVE_CRED},
                                             {"keep-caps", no_argument, NULL, OPT_KEEPCAPS},
                                             {"user-parent", no_argument, NULL, OPT_USER_PARENT},
                                             {NULL, 0, NULL, 0}};
    static const ul_excl_t excl[] = {/* rows and cols in ASCII order */
                                     {'W', 'w'},
                                     {0}};
    int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

    int c, namespaces = 0, setgroups_nerrs = 0, preserve_cred = 0;
    bool do_rd = false, do_wd = false, do_uid = false, force_uid = false, do_gid = false, force_gid = false,
         do_env = false, do_all = false, do_join_cgroup = false, do_user_parent = false;
    int do_fork = -1; /* unknown yet */
    char *wdns = NULL;
    uid_t uid = 0;
    gid_t gid = 0;
    int keepcaps = 0;
    int sock_fd = -1;
    int pid_fd = -1;

    /* yawl: do --preserve-credentials --user --mount by default (unless `enter=1` is specified) */
    if (pid_to_enter > 1) {
        namespace_target_pid = pid_to_enter;
        preserve_cred = 1;
        enable_namespace(CLONE_NEWUSER, NULL);
        enable_namespace(CLONE_NEWNS, NULL);
    }

    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    while ((c = getopt_long(argc, argv, "+ahVt:m::u::i::n::N:p::C::U::T::S:G:r::w::W::ecFZ", longopts, NULL)) != -1) {
        err_exclusive_options(c, longopts, excl, excl_st);

        switch (c) {
        case 'a':
            do_all = true;
            break;
        case 't':
            namespace_target_pid = strtoul_or_err(optarg, "failed to parse pid");
            break;
        case 'm':
            enable_namespace(CLONE_NEWNS, optarg);
            break;
        case 'u':
            enable_namespace(CLONE_NEWUTS, optarg);
            break;
        case 'i':
            enable_namespace(CLONE_NEWIPC, optarg);
            break;
        case 'n':
            enable_namespace(CLONE_NEWNET, optarg);
            break;
        case 'N':
            sock_fd = str2num_or_err(optarg, 10, "failed to parse file descriptor", 0, INT_MAX);
            break;
        case 'p':
            enable_namespace(CLONE_NEWPID, optarg);
            break;
        case 'C':
            enable_namespace(CLONE_NEWCGROUP, optarg);
            break;
        case 'U':
            enable_namespace(CLONE_NEWUSER, optarg);
            break;
        case 'T':
            enable_namespace(CLONE_NEWTIME, optarg);
            break;
        case 'S':
            if (strcmp(optarg, "follow") == 0)
                do_uid = true;
            else
                uid = strtoul_or_err(optarg, "failed to parse uid");
            force_uid = true;
            break;
        case 'G':
            if (strcmp(optarg, "follow") == 0)
                do_gid = true;
            else
                gid = strtoul_or_err(optarg, "failed to parse gid");
            force_gid = true;
            break;
        case 'F':
            do_fork = 0;
            break;
        case 'c':
            do_join_cgroup = true;
            break;
        case 'r':
            if (optarg)
                open_target_fd(&root_fd, "root", optarg);
            else
                do_rd = true;
            break;
        case 'w':
            if (optarg)
                open_target_fd(&wd_fd, "cwd", optarg);
            else
                do_wd = true;
            break;
        case 'W':
            wdns = optarg;
            break;
        case 'e':
            do_env = true;
            break;
        case OPT_PRESERVE_CRED:
            preserve_cred = 1;
            break;
        case OPT_KEEPCAPS:
            keepcaps = 1;
            break;
        case OPT_USER_PARENT:
            do_user_parent = true;
            break;
        case 'h':
            usage();
        default:
            LOG_ERROR_RET_MINUSONE("unrecognized option");
        }
    }

    if (do_all) {
        struct namespace_file *n = NULL;
        while ((n = next_nsfile(n, 0))) {
            if (n->enabled || !is_usable_namespace(namespace_target_pid, n))
                continue;
            enable_nsfile(n, NULL);
        }
    }

    /*
     * Open remaining namespace and directory descriptors.
     */
    namespaces = get_namespaces_without_fd();
    if (namespaces || sock_fd >= 0 || do_user_parent) {
        if (!namespace_target_pid)
            LOG_ERROR_RET_MINUSONE("no target PID specified");

        if (pid_fd < 0 && namespaces)
            open_namespaces(namespaces); /* fallback */
    }

    if (do_rd)
        open_target_fd(&root_fd, "root", NULL);
    if (do_wd)
        open_target_fd(&wd_fd, "cwd", NULL);
    if (do_env)
        open_target_fd(&env_fd, "environ", NULL);
    if (do_uid || do_gid)
        open_target_fd(&uid_gid_fd, "", NULL);
    if (do_join_cgroup) {
        if (!is_cgroup2())
            LOG_ERROR_RET_MINUSONE("--join-cgroup is only supported in cgroup v2");
        open_cgroup_procs();
    }

    /*
     * Get parent userns from any available ns.
     */
    if (do_user_parent)
        open_parent_user_ns_fd(pid_fd);

    if (sock_fd >= 0)
        open_target_sk_netns(pid_fd, sock_fd);

    /* All initialized, get final set of namespaces */
    namespaces = get_namespaces();
    if (!namespaces)
        LOG_ERROR_RET_MINUSONE("no namespace specified");

    if ((namespaces & CLONE_NEWPID) && do_fork == -1)
        do_fork = 1;

    /* for user namespaces we always set UID and GID (default is 0)
     * and clear root's groups if --preserve-credentials is no specified */
    if ((namespaces & CLONE_NEWUSER) && !preserve_cred) {
        force_uid = true, force_gid = true;

        /* We call setgroups() before and after we enter user namespace,
         * let's complain only if both fail */
        if (setgroups(0, NULL) != 0)
            setgroups_nerrs++;
    }

    /*
     * Now that we know which namespaces we want to enter, enter them.  Do
     * this in two passes, not entering the user namespace on the first
     * pass.  So if we're deprivileging the container we'll enter the user
     * namespace last and if we're privileging it then we enter the user
     * namespace first (because the initial setns will fail).
     */
    enter_namespaces(pid_fd, namespaces & ~CLONE_NEWUSER, 1); /* ignore errors */

    namespaces = get_namespaces();
    if (namespaces)
        enter_namespaces(pid_fd, namespaces, 0); /* report errors */

    if (pid_fd >= 0)
        close(pid_fd);

    /* Remember the current working directory if I'm not changing it */
    if (root_fd >= 0 && wd_fd < 0 && wdns == NULL) {
        wd_fd = open(".", O_RDONLY);
        if (wd_fd < 0)
            LOG_ERROR_RET_MINUSONE("cannot open current working directory");
    }

    /* Change the root directory */
    if (root_fd >= 0) {
        if (fchdir(root_fd) < 0)
            LOG_ERROR_RET_MINUSONE("change directory by root file descriptor failed");

        if (chroot(".") < 0)
            LOG_ERROR_RET_MINUSONE("chroot failed");
        if (chdir("/"))
            LOG_ERROR_RET_MINUSONE("cannot change directory to %s", "/");

        close(root_fd);
        root_fd = -1;
    }

    /* working directory specified as in-namespace path */
    if (wdns) {
        wd_fd = open(wdns, O_RDONLY);
        if (wd_fd < 0)
            LOG_ERROR_RET_MINUSONE("cannot open current working directory");
    }

    /* Change the working directory */
    if (wd_fd >= 0) {
        if (fchdir(wd_fd) < 0)
            LOG_ERROR_RET_MINUSONE("change directory by working directory file descriptor failed");

        close(wd_fd);
        wd_fd = -1;
    }

    /* Pass environment variables of the target process to the spawned process */
    if (env_fd >= 0) {
        struct ul_env_list *ls;

        ls = env_list_from_fd(env_fd);
        if (!ls && errno)
            LOG_ERROR_RET_MINUSONE("failed to get environment variables");
        clearenv();
        if (ls && env_list_setenv(ls, 0) < 0)
            LOG_ERROR_RET_MINUSONE("failed to set environment variables");
        env_list_free(ls);
        close(env_fd);
    }

    /* Join into the target cgroup */
    if (cgroup_procs_fd >= 0)
        join_into_cgroup();

    if (uid_gid_fd >= 0) {
        struct stat st;

        if (fstat(uid_gid_fd, &st) > 0)
            LOG_ERROR_RET_MINUSONE("can not get process stat");

        close(uid_gid_fd);
        uid_gid_fd = -1;

        if (do_uid)
            uid = st.st_uid;
        if (do_gid)
            gid = st.st_gid;
    }

    if (do_fork == 1)
        continue_as_child();

    if (force_uid || force_gid) {
        if (force_gid && setgroups(0, NULL) != 0 && setgroups_nerrs) /* drop supplementary groups */
            LOG_ERROR_RET_MINUSONE("setgroups failed");
        if (force_gid && setgid(gid) < 0) /* change GID */
            LOG_ERROR_RET_MINUSONE("setgid() failed");
        if (force_uid && setuid(uid) < 0) /* change UID */
            LOG_ERROR_RET_MINUSONE("setuid() failed");
    }

    if (keepcaps && (namespaces & CLONE_NEWUSER))
        cap_permitted_to_ambient();

    if (optind < argc)
        execvp(argv[optind], argv + optind);

    LOG_ERROR("failed to execute %s: %s", argv[optind], strerror(errno));
    return -1;
}

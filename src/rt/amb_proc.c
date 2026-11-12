/* src/rt/amb_proc.c -- the processes the engine runs: the reasoner, run once over a
   snapshot file or kept for a session, and the profile's keeper. A program runs by
   fork and execv, never through a shell, so no argument is quoted or expanded: a
   reasoner is reached only by the arguments and the requests the engine gives it.

   THE SANDBOX (amb_sandbox). A reasoner proves rules over a page's data, and should
   reach nothing but its own pipes and files. Between fork and exec the child takes on:
     - limits (setrlimit): its address space, its CPU time, the files it holds open,
       the size of a file it writes, and no core file;
     - no network: a network namespace of its own (unshare; as a user who may not, a
       user namespace with it), whose one device is a loopback that is down;
     - no file written outside its working directory: Landlock, where the kernel has
       it, lets it write beneath that directory alone, and from Landlock's ABI 4 makes
       no TCP connection, from ABI 6 signals no process and reaches no abstract socket
       outside its domain. Where the kernel has no Landlock, the syscall filter refuses
       every open for writing, and every call that makes, moves or removes a file;
     - a seccomp-bpf filter that allows the calls cocolog's runtime makes (those it
       made over every gate, traced) and kills the process on any other, socket(2)
       among them, of every family: the runtime needs none, a Unix socket would reach
       the host's services through their files, and cocolog's own libraries take a
       refused socket for a connection that failed, where the end of the process is
       what the engine reports. A new process (fork, vfork, clone without
       CLONE_THREAD, clone3) is refused, and so is every execve but the one that starts
       the program, known by its path's address. kill and tgkill reach the process
       itself alone; prlimit64 reads a limit and sets none; ioctl answers a terminal's
       questions and no other.
   A child whose sandbox cannot be made exits 125 before it runs anything.

   THE SESSION'S REASONER (amb_reasoner). cocolog over prolog/amb/reason.pl serve, its
   requests on its standard input, its answers on its standard output, each answer
   ended by amb_done(NONCE, Status). It is given its first snapshot whole and then
   deltas (amb_delta_write): the facts retracted and added since the one it holds.

   THE DELTA. A snapshot is streamed a module's section at a time, each section a
   table's records in slot order and each record's facts in the contract's order, so
   a predicate's facts are a run in the order a reasoner loads them. The delta compares
   the old and new run of each predicate (a hash a line, the common head and tail, and
   Myers' diff of what is between, bounded): the facts kept, retracted and added. A
   predicate whose added facts all come after its kept ones, and whose retracted facts
   have no twin, is edited (each fact retracted, each added one appended); any other is
   rebuilt from runs of its kept facts and the added ones, in the new order. Either way
   the reasoner holds each predicate's facts in the order a load of the whole new
   snapshot gives, so it answers as a fresh one would. */
#include <amb.pl.h>
#include "amb_proc.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <vector.pl>

/* ---- the sandbox ------------------------------------------------------------ */

/* the constants the sandbox needs, as numbers: <sched.h> and <linux/*.h> name them
   under feature macros cicilang's reading of the system headers does not set
   (toolchain/REPORT.md, finding 11) */
#define SB_CLONE_THREAD 0x00010000u
#define SB_CLONE_NEWUSER 0x10000000
#define SB_CLONE_NEWNET 0x40000000
#define SB_PR_SET_NO_NEW_PRIVS 38
#define SB_PR_SET_SECCOMP 22
#define SB_SECCOMP_MODE_FILTER 2
#define SB_RET_KILL 0x80000000u
#define SB_RET_ERRNO 0x00050000u
#define SB_RET_ALLOW 0x7fff0000u
#define SB_O_PATH 010000000
#define SB_O_WRITING 03103u               /* O_WRONLY O_RDWR O_CREAT O_TRUNC O_APPEND */
#define SB_LL_CREATE 444
#define SB_LL_ADD 445
#define SB_LL_RESTRICT 446

/* <unistd.h> declares pipe2 under _GNU_SOURCE alone */
int pipe2(int fds[2], int flags);

#if defined(__x86_64__)
#define SB_ARCH 0xC000003Eu                /* AUDIT_ARCH_X86_64 */
#elif defined(__aarch64__)
#define SB_ARCH 0xC00000B7u                /* AUDIT_ARCH_AARCH64 */
#else
#define SB_ARCH 0u
#endif

/* a BPF instruction and a program, as <linux/filter.h> lays them out */
typedef struct sb_insn { uint16_t code; uint8_t jt, jf; uint32_t k; } sb_insn;
typedef struct sb_prog { unsigned short len; sb_insn *filter; } sb_prog;

#define SB_CAP 400
static void sb_put(sb_insn *f, int *n, uint16_t code, uint8_t jt, uint8_t jf, uint32_t k) {
    if (*n >= SB_CAP) return;
    f[*n].code = code;
    f[*n].jt = jt;
    f[*n].jf = jf;
    f[*n].k = k;
    (*n)++;
}
/* load the word of struct seccomp_data at off: 0 the call's number, 4 its arch, 16 + 8i
   the low word of argument i, 20 + 8i its high word */
static void sb_ld(sb_insn *f, int *n, uint32_t off) { sb_put(f, n, 0x20, 0, 0, off); }
static void sb_jeq(sb_insn *f, int *n, uint32_t k, uint8_t t, uint8_t e) { sb_put(f, n, 0x15, t, e, k); }
static void sb_ret(sb_insn *f, int *n, uint32_t k) { sb_put(f, n, 0x06, 0, 0, k); }
/* the call nr answered so */
static void sb_rule(sb_insn *f, int *n, long nr, uint32_t ret) {
    if (nr < 0) return;
    sb_jeq(f, n, (uint32_t)nr, 0, 1);
    sb_ret(f, n, ret);
}

/* the calls cocolog's runtime makes, allowed as they are */
static const long sb_allowed[] = {
    SYS_read, SYS_write, SYS_readv, SYS_writev, SYS_pread64, SYS_pwrite64, SYS_lseek, SYS_close,
    SYS_fstat, SYS_newfstatat, SYS_statx, SYS_faccessat, SYS_readlinkat, SYS_getcwd, SYS_getdents64,
    SYS_fcntl, SYS_dup, SYS_dup3, SYS_ppoll, SYS_pselect6, SYS_fstatfs, SYS_statfs,
    SYS_mmap, SYS_munmap, SYS_mremap, SYS_mprotect, SYS_madvise, SYS_brk, SYS_msync,
    SYS_rt_sigaction, SYS_rt_sigprocmask, SYS_rt_sigreturn, SYS_sigaltstack, SYS_restart_syscall,
    SYS_futex, SYS_set_robust_list, SYS_get_robust_list, SYS_set_tid_address, SYS_rseq,
    SYS_exit, SYS_exit_group, SYS_wait4, SYS_getpid, SYS_gettid, SYS_getppid, SYS_getpgid,
    SYS_getuid, SYS_geteuid, SYS_getgid, SYS_getegid, SYS_getresuid, SYS_getresgid, SYS_getgroups,
    SYS_getrandom, SYS_clock_gettime, SYS_clock_getres, SYS_gettimeofday, SYS_nanosleep, SYS_clock_nanosleep,
    SYS_sched_yield, SYS_sched_getaffinity, SYS_uname, SYS_getrusage, SYS_times, SYS_sysinfo, SYS_umask,
    SYS_membarrier, SYS_fadvise64, SYS_fsync, SYS_fdatasync, SYS_ftruncate, SYS_flock,
#if defined(__x86_64__)
    SYS_stat, SYS_lstat, SYS_access, SYS_readlink, SYS_dup2, SYS_poll, SYS_select, SYS_getrlimit,
    SYS_arch_prctl, SYS_time, SYS_getpgrp, SYS_faccessat2,
#endif
};
/* the calls that make, move or remove a file: Landlock decides each, beneath the
   working directory; without Landlock each is refused */
static const long sb_files[] = {
    SYS_unlinkat, SYS_renameat, SYS_renameat2, SYS_mkdirat, SYS_symlinkat, SYS_linkat,
#if defined(__x86_64__)
    SYS_unlink, SYS_rename, SYS_mkdir, SYS_rmdir, SYS_symlink, SYS_link,
#endif
};
/* the calls refused with an error, not killed: the runtime meets their refusal */
static const long sb_refused[] = {
    SYS_execveat, SYS_setrlimit, SYS_prctl, SYS_mknodat, SYS_fchmodat, SYS_fchmod, SYS_fchownat, SYS_fchown,
    SYS_utimensat, SYS_truncate, SYS_setpgid, SYS_setsid,
#if defined(__x86_64__)
    SYS_fork, SYS_vfork, SYS_creat, SYS_mknod, SYS_chmod, SYS_chown, SYS_lchown, SYS_utimes, SYS_futimesat,
#endif
};

/* the filter: the program at path (its pointer, the one execv is given) started, the
   process itself pid; Landlock in force or not */
static int sb_filter(const char *path, pid_t pid, int landlock) {
    sb_insn f[SB_CAP];
    int n = 0;
    uint64_t pp = (uint64_t)(uintptr_t)path;
    sb_ld(f, &n, 4);
    sb_jeq(f, &n, SB_ARCH, 1, 0);
    sb_ret(f, &n, SB_RET_KILL);
    sb_ld(f, &n, 0);
#if defined(__x86_64__)
    sb_put(f, &n, 0x35, 0, 1, 0x40000000u);          /* x32 calls */
    sb_ret(f, &n, SB_RET_KILL);
#endif
    /* a thread, and no process */
    sb_jeq(f, &n, (uint32_t)SYS_clone, 0, 4);
    sb_ld(f, &n, 16);
    sb_put(f, &n, 0x45, 0, 1, SB_CLONE_THREAD);
    sb_ret(f, &n, SB_RET_ALLOW);
    sb_ret(f, &n, SB_RET_ERRNO | EPERM);
    sb_rule(f, &n, SYS_clone3, SB_RET_ERRNO | ENOSYS);
    /* the program's own execve, and no other */
    sb_jeq(f, &n, (uint32_t)SYS_execve, 0, 6);
    sb_ld(f, &n, 16);
    sb_jeq(f, &n, (uint32_t)pp, 0, 3);
    sb_ld(f, &n, 20);
    sb_jeq(f, &n, (uint32_t)(pp >> 32), 0, 1);
    sb_ret(f, &n, SB_RET_ALLOW);
    sb_ret(f, &n, SB_RET_ERRNO | EPERM);
    /* a signal to itself alone */
    static const long sigs[] = { SYS_kill, SYS_tgkill, SYS_tkill };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        sb_jeq(f, &n, (uint32_t)sigs[i], 0, 4);
        sb_ld(f, &n, 16);
        sb_jeq(f, &n, (uint32_t)pid, 0, 1);
        sb_ret(f, &n, SB_RET_ALLOW);
        sb_ret(f, &n, SB_RET_ERRNO | EPERM);
    }
    /* limits read, none set */
    sb_jeq(f, &n, (uint32_t)SYS_prlimit64, 0, 6);
    sb_ld(f, &n, 32);
    sb_jeq(f, &n, 0, 0, 3);
    sb_ld(f, &n, 36);
    sb_jeq(f, &n, 0, 0, 1);
    sb_ret(f, &n, SB_RET_ALLOW);
    sb_ret(f, &n, SB_RET_ERRNO | EPERM);
    /* a terminal's questions: TCGETS, TIOCGWINSZ, FIONREAD, FIONCLEX, FIOCLEX */
    sb_jeq(f, &n, (uint32_t)SYS_ioctl, 0, 8);
    sb_ld(f, &n, 24);
    sb_jeq(f, &n, 0x5401, 5, 0);
    sb_jeq(f, &n, 0x5413, 4, 0);
    sb_jeq(f, &n, 0x541B, 3, 0);
    sb_jeq(f, &n, 0x5450, 2, 0);
    sb_jeq(f, &n, 0x5451, 1, 0);
    sb_ret(f, &n, SB_RET_ERRNO | ENOTTY);
    sb_ret(f, &n, SB_RET_ALLOW);
    /* opening a file: Landlock decides a write, or the filter refuses it */
    if (landlock) {
        sb_rule(f, &n, SYS_openat, SB_RET_ALLOW);
#if defined(__x86_64__)
        sb_rule(f, &n, SYS_open, SB_RET_ALLOW);
#endif
    } else {
        sb_jeq(f, &n, (uint32_t)SYS_openat, 0, 4);
        sb_ld(f, &n, 32);
        sb_put(f, &n, 0x45, 0, 1, SB_O_WRITING);
        sb_ret(f, &n, SB_RET_ERRNO | EACCES);
        sb_ret(f, &n, SB_RET_ALLOW);
#if defined(__x86_64__)
        sb_jeq(f, &n, (uint32_t)SYS_open, 0, 4);
        sb_ld(f, &n, 24);
        sb_put(f, &n, 0x45, 0, 1, SB_O_WRITING);
        sb_ret(f, &n, SB_RET_ERRNO | EACCES);
        sb_ret(f, &n, SB_RET_ALLOW);
#endif
    }
    for (size_t i = 0; i < sizeof sb_files / sizeof sb_files[0]; i++) sb_rule(f, &n, sb_files[i], landlock ? SB_RET_ALLOW : SB_RET_ERRNO | EACCES);
    for (size_t i = 0; i < sizeof sb_refused / sizeof sb_refused[0]; i++)
        sb_rule(f, &n, sb_refused[i], SB_RET_ERRNO | EPERM);
    for (size_t i = 0; i < sizeof sb_allowed / sizeof sb_allowed[0]; i++) sb_rule(f, &n, sb_allowed[i], SB_RET_ALLOW);
    sb_ret(f, &n, SB_RET_KILL);
    if (n >= SB_CAP || SB_ARCH == 0) return -1;
    sb_prog prog = { (unsigned short)n, f };
    return prctl(SB_PR_SET_SECCOMP, SB_SECCOMP_MODE_FILTER, &prog, 0, 0);
}

/* Landlock: writing beneath dir alone (and, as the ABI allows, no TCP, no signal or
   abstract socket outside); its ABI, or 0 where the kernel has none */
static int sb_landlock(const char *dir) {
    long abi = syscall(SB_LL_CREATE, (void *)0, (size_t)0, (uint32_t)1);
    if (abi < 1) return 0;
    uint64_t fs = (1u << 1) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12);
    if (abi >= 2) fs |= 1u << 13;       /* REFER */
    if (abi >= 3) fs |= 1u << 14;       /* TRUNCATE */
    if (abi >= 5) fs |= 1u << 15;       /* IOCTL_DEV */
    uint64_t attr[3];
    attr[0] = fs;
    attr[1] = abi >= 4 ? 3u : 0u;       /* BIND_TCP, CONNECT_TCP */
    attr[2] = abi >= 6 ? 3u : 0u;       /* ABSTRACT_UNIX_SOCKET, SIGNAL */
    size_t size = abi >= 6 ? 24 : abi >= 4 ? 16 : 8;
    long rs = syscall(SB_LL_CREATE, attr, size, (uint32_t)0);
    if (rs < 0) return -1;
    int d = open(dir, SB_O_PATH | O_CLOEXEC);
    if (d < 0) { close((int)rs); return -1; }
    unsigned char rule[12];                 /* struct landlock_path_beneath_attr, packed */
    int32_t pfd = d;
    memcpy(rule, &fs, 8);
    memcpy(rule + 8, &pfd, 4);
    long ok = syscall(SB_LL_ADD, (int)rs, 1, rule, (uint32_t)0);
    close(d);
    if (ok < 0 || syscall(SB_LL_RESTRICT, (int)rs, (uint32_t)0) < 0) { close((int)rs); return -1; }
    close((int)rs);
    return (int)abi;
}

/* a network namespace of its own: 1 as one may make one, 2 with a user namespace,
   0 none (the filter's refusal of sockets is then the only wall) */
static int sb_network(void) {
    if (syscall(SYS_unshare, SB_CLONE_NEWNET) == 0) return 1;
    uid_t u = getuid();
    gid_t g = getgid();
    if (syscall(SYS_unshare, SB_CLONE_NEWUSER | SB_CLONE_NEWNET) != 0) return 0;
    char line[64];
    int fd = open("/proc/self/setgroups", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) { (void)!write(fd, "deny", 4); close(fd); }
    snprintf(line, sizeof line, "%u %u 1\n", (unsigned)u, (unsigned)u);
    fd = open("/proc/self/uid_map", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) { (void)!write(fd, line, strlen(line)); close(fd); }
    snprintf(line, sizeof line, "%u %u 1\n", (unsigned)g, (unsigned)g);
    fd = open("/proc/self/gid_map", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) { (void)!write(fd, line, strlen(line)); close(fd); }
    return 2;
}

static void sb_limit(int what, uint64_t soft, uint64_t hard) {
    struct rlimit rl;
    rl.rlim_cur = (rlim_t)soft;
    rl.rlim_max = (rlim_t)hard;
    setrlimit(what, &rl);
}

/* what the child took on */
typedef struct sb_layers { int net, landlock, filter; } sb_layers;

/* the sandbox entered, in the child between fork and exec; path the program it then
   runs. 0 entered, -1 not */
static int sb_enter(const amb_sandbox *sb, const char *path, sb_layers *got) {
    got->net = 0;
    got->landlock = 0;
    got->filter = 0;
    if (chdir(sb->dir) != 0) return -1;
    sb_limit(RLIMIT_CORE, 0, 0);
    sb_limit(RLIMIT_AS, sb->mem, sb->mem);
    sb_limit(RLIMIT_CPU, sb->cpu, sb->cpu + 5);
    sb_limit(RLIMIT_NOFILE, sb->files, sb->files);
    sb_limit(RLIMIT_FSIZE, sb->fsize, sb->fsize);
    got->net = sb_network();
    if (prctl(SB_PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return -1;
    got->landlock = sb_landlock(sb->dir);
    if (got->landlock < 0) return -1;
    if (sb_filter(path, getpid(), got->landlock > 0) != 0) return -1;
    got->filter = 1;
    return 0;
}

/* a size, as AMB_REASON_MEM writes one: bytes, or with K, M or G */
static uint64_t sb_env(const char *name, uint64_t dflt) {
    const char *v = getenv(name);
    if (!v || !*v) return dflt;
    char *end = 0;
    unsigned long long x = strtoull(v, &end, 10);
    uint64_t m = 1;
    if (end && (*end == 'K' || *end == 'k')) m = 1024;
    if (end && (*end == 'M' || *end == 'm')) m = 1024 * 1024;
    if (end && (*end == 'G' || *end == 'g')) m = 1024ull * 1024 * 1024;
    return (uint64_t)x * m;
}

void amb_sandbox_init(amb_sandbox *sb, const char *dir) {
    const char *on = getenv("AMB_SANDBOX");
    sb->on = !(on && on[0] == '0');
    sb->mem = sb_env("AMB_REASON_MEM", 4ull << 30);
    sb->cpu = sb_env("AMB_REASON_CPU", 600);
    sb->timeout = sb_env("AMB_REASON_TIMEOUT", 600);
    sb->files = sb_env("AMB_REASON_FILES", 64);
    sb->fsize = sb_env("AMB_REASON_FSIZE", 256ull << 20);
    snprintf(sb->dir, sizeof sb->dir, "%s", dir);
    mkdir(sb->dir, 0700);
}

/* ---- running a program ------------------------------------------------------- */

/* every descriptor from 3 up closed: a child keeps no other reasoner's pipe */
static void close_from_3(void) {
#ifdef SYS_close_range
    if (syscall(SYS_close_range, 3u, ~0u, 0u) == 0) return;
#endif
    for (int fd = 3; fd < 1024; fd++) close(fd);
}

/* the program's file: argv[0], or found on PATH; 0 when found */
static int find_program(const char *name, char *out, size_t cap) {
    if (strchr(name, '/')) { snprintf(out, cap, "%s", name); return 0; }
    const char *path = getenv("PATH");
    if (!path) return -1;
    size_t i = 0;
    while (path[i]) {
        size_t e = i;
        while (path[e] && path[e] != ':') e++;
        snprintf(out, cap, "%.*s/%s", (int)(e - i), path + i, name);
        if (access(out, X_OK) == 0) return 0;
        i = path[e] ? e + 1 : e;
    }
    return -1;
}

/* in the child: the descriptors in place, the sandbox entered, the program run */
static void child_run(const char *const argv[], int in, int out, int err, const amb_sandbox *sb) {
    if (in != 0) { dup2(in, 0); }
    if (out != 1) { dup2(out, 1); }
    if (err >= 0 && err != 2) { dup2(err, 2); }
    close_from_3();
    char path[2048];
    if (find_program(argv[0], path, sizeof path) != 0) _exit(127);
    if (sb && sb->on) {
        sb_layers got;
        if (sb_enter(sb, path, &got) != 0) {
            const char *m = "ambitious: the reasoner's sandbox could not be made\n";
            (void)!write(2, m, strlen(m));
            _exit(125);
        }
    }
    execv(path, (char *const *)argv);
    _exit(127);
}

/* how a process ended, from its wait status */
static void ending_of(int st, amb_ending *how) {
    how->ended = 1;
    if (WIFEXITED(st)) {
        how->exited = 1;
        how->code = WEXITSTATUS(st);
        if (how->code == 125) snprintf(how->why, sizeof how->why, "its sandbox could not be made (exit 125)");
        else if (how->code == 127) snprintf(how->why, sizeof how->why, "its program could not be run (exit 127)");
        else snprintf(how->why, sizeof how->why, "exited %d", how->code);
        return;
    }
    how->exited = 0;
    how->code = WIFSIGNALED(st) ? WTERMSIG(st) : 0;
    const char *what = "a signal";
    if (how->code == SIGSYS) what = "its syscall filter (SIGSYS): a call the sandbox does not allow";
    else if (how->code == SIGXCPU) what = "its CPU time limit (SIGXCPU)";
    else if (how->code == SIGXFSZ) what = "its file size limit (SIGXFSZ)";
    else if (how->code == SIGKILL) what = "SIGKILL: its CPU time's hard limit, or the engine for an answer past its time";
    else if (how->code == SIGSEGV) what = "SIGSEGV: out of memory under its limit, or a fault";
    else if (how->code == SIGABRT) what = "SIGABRT: out of memory under its limit, or an abort";
    snprintf(how->why, sizeof how->why, "killed by %s (signal %d)", what, how->code);
}

int amb_run_in(const char *const argv[], const char *out_path, const char *err_path, const amb_sandbox *sb, amb_ending *how) {
    int in = open("/dev/null", O_RDONLY | O_CLOEXEC);
    int out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    int err = err_path ? open(err_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600) : -1;
    if (in < 0 || out < 0 || (err_path && err < 0)) {
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        if (err >= 0) close(err);
        return -1;
    }
    pid_t pid = fork();
    if (pid == 0) child_run(argv, in, out, err, sb);
    close(in);
    close(out);
    if (err >= 0) close(err);
    if (pid < 0) return -1;
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    amb_ending e;
    ending_of(st, &e);
    if (how) *how = e;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
}

int amb_run(const char *const argv[], const char *out_path, const char *err_path) {
    return amb_run_in(argv, out_path, err_path, 0, 0);
}

/* a line to the descriptor fd */
static void say(int fd, const char *m) { (void)!write(fd, m, strlen(m)); }

/* the sandbox tried from inside: a child enters it and asks for each thing it should be
   refused, saying each before it asks, the socket last since it ends the child; the
   lines into out */
int amb_sandbox_report(const amb_sandbox *sb, FILE *out) {
    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) return -1;
    fflush(out);
    pid_t pid = fork();
    if (pid == 0) {
        close(p[0]);
        sb_layers got;
        char path[16];
        snprintf(path, sizeof path, "/bin/true");
        if (sb_enter(sb, path, &got) != 0) { say(p[1], "the sandbox could not be made\n"); _exit(125); }
        char m[512];
        snprintf(m, sizeof m, "limits: address space %llu MiB, CPU %llu s, %llu open files, files of %llu MiB at most, no core file\n",
                 (unsigned long long)(sb->mem >> 20), (unsigned long long)sb->cpu, (unsigned long long)sb->files,
                 (unsigned long long)(sb->fsize >> 20));
        say(p[1], m);
        static const char *const nets[] = { "network: none of its own (the syscall filter is the wall)\n",
                                            "network: a namespace of its own, no device up\n",
                                            "network: a user and a network namespace of its own, no device up\n" };
        say(p[1], nets[got.net]);
        if (got.landlock) {
            snprintf(m, sizeof m, "file writes: Landlock (ABI %d), beneath its working directory alone\n", got.landlock);
            say(p[1], m);
        } else say(p[1], "file writes: none (no Landlock here: the filter refuses every open for writing)\n");
        say(p[1], "syscall filter: on; a call it does not allow ends the process\n");
        int fd = open("/tmp/ambitious-sandbox-probe", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        say(p[1], fd < 0 ? "a file written in /tmp: refused\n" : "a file written in /tmp: WRITTEN\n");
        if (fd >= 0) { close(fd); unlink("/tmp/ambitious-sandbox-probe"); }
        fd = open("probe", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        say(p[1], fd < 0 ? "a file written in its working directory: refused\n" : "a file written in its working directory: written\n");
        if (fd >= 0) { close(fd); unlink("probe"); }
        long c = syscall(SYS_clone, (unsigned long)SIGCHLD, 0, 0, 0, 0);
        if (c == 0) _exit(0);
        say(p[1], c < 0 && errno == EPERM ? "a new process: refused\n" : "a new process: MADE\n");
        say(p[1], kill(getppid(), 0) != 0 ? "a signal to the engine: refused\n" : "a signal to the engine: SENT\n");
        say(p[1], "a socket: ");
        long so = syscall(SYS_socket, 1, 1, 0);
        say(p[1], so < 0 ? "refused\n" : "MADE\n");
        _exit(0);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); return -1; }
    char buf[2048];
    ssize_t n;
    while ((n = read(p[0], buf, sizeof buf)) > 0) fwrite(buf, 1, (size_t)n, out);
    close(p[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGSYS) fprintf(out, "the process ended by its syscall filter (SIGSYS)\n");
    else if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { fprintf(out, "the probe ended otherwise\n"); return -1; }
    return 0;
}

/* ---- the delta ---------------------------------------------------------------- */

/* a line of a snapshot: its hash, where it is, its predicate's group */
typedef struct dl_line { uint64_t hash; uint32_t off, len, group, fact; } dl_line;
/* a section: its header's line, its module's name in the text, its document */
typedef struct dl_sec { uint32_t line, mod_off, mod_len; uint64_t doc; } dl_sec;
/* a predicate of a section: its name (in the names pool), its lines' runs in the old
   and the new text */
typedef struct dl_group { uint64_t hash; uint32_t sec, name_off, name_len, na, nb, a0, b0; } dl_group;
/* a run of facts kept: from old position a and new position b, len of them */
typedef struct dl_run { uint32_t a, b, len; } dl_run;

/* the end of the line from i: its newline's place, or n */
static size_t dl_eol(const char *t, size_t n, size_t i) {
    const char *q = (const char *)memchr(t + i, '\n', n - i);
    return q ? (size_t)(q - t) : n;
}

static uint64_t dl_hash(const char *p, size_t n) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ (uint64_t)n;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        h = (h ^ w) * 0xff51afd7ed558ccdull;
        h ^= h >> 32;
    }
    uint64_t w = 0;
    memcpy(&w, p + i, n - i);
    h = (h ^ w) * 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 29;
    return h;
}

/* the group of the predicate named t[off, off+n) in section sec, made if need be: its
   number, or UINT32_MAX when the table is full */
static uint32_t dl_group_of(vector<dl_group> *gs, vector<uint32_t> *slots, vector<char> *names, uint32_t sec, const char *t, uint32_t off,
                            uint32_t n) {
    uint64_t h = dl_hash(t + off, n) ^ ((uint64_t)sec * 0x9E3779B97F4A7C15ull);
    size_t mask = len(*slots) - 1;
    for (size_t i = (size_t)h & mask, probes = 0; probes < len(*slots); i = (i + 1) & mask, probes++) {
        uint32_t g = at(*slots, i);
        if (!g) {
            if (len(*gs) * 2 >= len(*slots)) return UINT32_MAX;
            dl_group e = { h, sec, (uint32_t)len(*names), n, 0, 0, 0, 0 };
            append_range(names, t + off, n);
            push_back(gs, e);
            (*slots)[i] = (uint32_t)len(*gs);
            return (uint32_t)len(*gs) - 1;
        }
        dl_group e = at(*gs, g - 1);
        if (e.hash == h && e.sec == sec && e.name_len == n && memcmp(data(*names) + e.name_off, t + off, n) == 0) return g - 1;
    }
    return UINT32_MAX;
}

/* a snapshot's lines and sections, each fact's line in its group: 0, or -1 when the
   text is no snapshot of lines a delta can follow */
static int dl_index(const char *t, size_t n, vector<dl_line> *lines, vector<dl_sec> *secs, vector<dl_group> *gs, vector<uint32_t> *slots,
                    vector<char> *names) {
    if (n >= 0xFFFFFFF0u) return -1;
    size_t i = 0;
    uint32_t facts = 0;
    while (i < n) {
        size_t e = dl_eol(t, n, i);
        uint32_t ln = (uint32_t)(e - i);
        if (ln == 0) { i = e + 1; continue; }
        if (t[e - 1] != '.') return -1;
        if (ln > 13 && memcmp(t + i, "amb_snapshot(", 13) == 0) {
            size_t m = i + 13, me = m;
            while (me < e && t[me] != ',') me++;
            size_t d = me + 1;
            while (d < e && t[d] == ' ') d++;
            uint64_t doc = 0;
            while (d < e && t[d] >= '0' && t[d] <= '9') { doc = doc * 10 + (uint64_t)(t[d] - '0'); d++; }
            dl_sec s = { (uint32_t)len(*lines), (uint32_t)m, (uint32_t)(me - m), doc };
            push_back(secs, s);
            dl_line l = { dl_hash(t + i, ln), (uint32_t)i, ln, UINT32_MAX, 0 };
            push_back(lines, l);
        } else {
            if (len(*secs) == 0) return -1;
            const char *q = (const char *)memchr(t + i, '(', e - i);
            size_t p = q ? (size_t)(q - t) : e;
            if (p == e || p == i) return -1;
            uint32_t g = dl_group_of(gs, slots, names, (uint32_t)len(*secs) - 1, t, (uint32_t)i, (uint32_t)(p - i));
            if (g == UINT32_MAX) return -1;
            dl_line l = { dl_hash(t + i, ln), (uint32_t)i, ln, g, facts++ };
            push_back(lines, l);
        }
        i = e + 1;
    }
    return 0;
}

static int dl_same(const char *ot, dl_line a, const char *nt, dl_line b) {
    return a.hash == b.hash && a.len == b.len && memcmp(ot + a.off, nt + b.off, a.len) == 0;
}

/* the longest edit's bound: past it, Myers' diff gives up and the facts between the
   common head and tail are taken as all changed */
#define DL_DMAX 256

/* the runs of facts kept between old A[0, na) and new B[0, nb) (positions into ol and
   nl), into runs in order: the common head and tail, and between them Myers' shortest
   edit, when it is no longer than DL_DMAX */
static void dl_diff(const char *ot, const vector<dl_line> *ol, const uint32_t *A, uint32_t na, const char *nt, const vector<dl_line> *nl,
                    const uint32_t *B, uint32_t nb, vector<dl_run> *runs, vector<int32_t> *v, vector<int32_t> *trace) {
    clear(*runs);
    const dl_line *OL = data(*ol);
    const dl_line *NL = data(*nl);
    uint32_t p = 0;
    while (p < na && p < nb && dl_same(ot, OL[A[p]], nt, NL[B[p]])) p++;
    uint32_t s = 0;
    while (s < na - p && s < nb - p && dl_same(ot, OL[A[na - 1 - s]], nt, NL[B[nb - 1 - s]])) s++;
    if (p) { dl_run r = { 0, 0, p }; push_back(runs, r); }
    int32_t N = (int32_t)(na - p - s), M = (int32_t)(nb - p - s);
    if (N > 0 && M > 0) {
        /* Myers: V[k] the furthest x on diagonal k, for d = 0, 1, ...; each round's V kept */
        int32_t off = DL_DMAX + 1;
        resize(*v, (size_t)(2 * DL_DMAX + 3), 0);
        for (size_t i = 0; i < len(*v); i++) (*v)[i] = 0;
        clear(*trace);
        int32_t found = -1;
        for (int32_t d = 0; d <= DL_DMAX && found < 0; d++) {
            int32_t *V = data(*v);
            for (int32_t k = -d; k <= d; k += 2) {
                int32_t x;
                if (k == -d || (k != d && V[off + k - 1] < V[off + k + 1])) x = V[off + k + 1];
                else x = V[off + k - 1] + 1;
                int32_t y = x - k;
                while (x < N && y < M && dl_same(ot, OL[A[p + (uint32_t)x]], nt, NL[B[p + (uint32_t)y]])) { x++; y++; }
                V[off + k] = x;
                if (x >= N && y >= M) { found = d; break; }
            }
            append_range(trace, data(*v), len(*v));
        }
        if (found >= 0) {
            /* back from (N, M): each round's snake, then the step that led to it */
            size_t mark = len(*runs);
            int32_t x = N, y = M;
            size_t w = len(*v);
            for (int32_t d = found; d > 0; d--) {
                const int32_t *Vp = data(*trace) + (size_t)(d - 1) * w;
                int32_t k = x - y;
                int32_t pk = (k == -d || (k != d && Vp[off + k - 1] < Vp[off + k + 1])) ? k + 1 : k - 1;
                int32_t px = Vp[off + pk], py = px - pk;
                int32_t mx = pk == k + 1 ? px : px + 1;
                if (x > mx) { dl_run r = { p + (uint32_t)mx, p + (uint32_t)(mx - k), (uint32_t)(x - mx) }; push_back(runs, r); }
                x = px;
                y = py;
            }
            if (x > 0) { dl_run r = { p, p, (uint32_t)x }; push_back(runs, r); }
            /* the middle's runs were found back to front */
            size_t lo = mark, hi = len(*runs);
            while (hi > lo + 1) {
                dl_run t = at(*runs, lo);
                (*runs)[lo] = at(*runs, hi - 1);
                (*runs)[hi - 1] = t;
                lo++;
                hi--;
            }
        }
    }
    if (s) { dl_run r = { na - s, nb - s, s }; push_back(runs, r); }
}

static void put_s(vector<char> *o, const char *s) { append_range(o, s, strlen(s)); }
static void put_line(vector<char> *o, const char *pre, const char *t, dl_line l) {
    put_s(o, pre);
    append_range(o, t + l.off, l.len - 1);      /* the fact without its full stop */
    put_s(o, ").\n");
}

/* the delta from old to nw into o (after what o holds), its first line head: 1 written,
   0 the same, 2 none fits */
static int dl_build(vector<char> *o, const char *head, const char *ot, size_t on, const char *nt, size_t nn, amb_delta_stats *st) {
    memset(st, 0, sizeof *st);
    if (on == nn && memcmp(ot, nt, on) == 0) return 0;
    vector<dl_line> ol;
    vector<dl_line> nl;
    vector<dl_sec> os;
    vector<dl_sec> ns;
    vector<dl_group> gs;
    vector<uint32_t> slots;
    vector<char> names;
    resize(slots, 4096, 0);
    if (dl_index(ot, on, &ol, &os, &gs, &slots, &names) != 0) return 2;
    uint32_t old_groups = (uint32_t)len(gs);
    if (dl_index(nt, nn, &nl, &ns, &gs, &slots, &names) != 0) return 2;
    /* the same sections, in the same order, each module once */
    if (len(os) != len(ns) || len(os) == 0) return 2;
    for (size_t i = 0; i < len(os); i++) {
        dl_sec a = at(os, i), b = at(ns, i);
        if (a.doc != b.doc || a.mod_len != b.mod_len || memcmp(ot + a.mod_off, nt + b.mod_off, a.mod_len) != 0) return 2;
        for (size_t j = 0; j < i; j++) {
            dl_sec c = at(os, j);
            if (c.mod_len == a.mod_len && memcmp(ot + c.mod_off, ot + a.mod_off, a.mod_len) == 0) return 2;
        }
    }
    (void)old_groups;
    /* each group's lines in order: counted, then placed */
    size_t G = len(gs);
    for (size_t i = 0; i < len(ol); i++) { dl_line l = at(ol, i); if (l.group != UINT32_MAX) { st->old_facts++; gs[l.group].na++; } }
    for (size_t i = 0; i < len(nl); i++) { dl_line l = at(nl, i); if (l.group != UINT32_MAX) { st->new_facts++; gs[l.group].nb++; } }
    uint32_t ca = 0, cb = 0;
    for (size_t g = 0; g < G; g++) {
        gs[g].a0 = ca;
        gs[g].b0 = cb;
        ca += at(gs, g).na;
        cb += at(gs, g).nb;
    }
    vector<uint32_t> A;
    vector<uint32_t> B;
    vector<uint32_t> fill;
    resize(A, (size_t)ca + 1, 0);
    resize(B, (size_t)cb + 1, 0);
    resize(fill, G + 1, 0);
    for (size_t i = 0; i < len(ol); i++) {
        dl_line l = at(ol, i);
        if (l.group == UINT32_MAX) continue;
        A[at(gs, l.group).a0 + fill[l.group]] = (uint32_t)i;
        fill[l.group]++;
    }
    for (size_t g = 0; g <= G; g++) fill[g] = 0;
    for (size_t i = 0; i < len(nl); i++) {
        dl_line l = at(nl, i);
        if (l.group == UINT32_MAX) continue;
        B[at(gs, l.group).b0 + fill[l.group]] = (uint32_t)i;
        fill[l.group]++;
    }
    /* each section's header, then its groups that changed */
    vector<dl_run> runs;
    vector<int32_t> v;
    vector<int32_t> trace;
    vector<uint8_t> covered;
    put_s(o, head);
    double cost = 0;
    for (size_t sec = 0; sec < len(ns); sec++) {
        dl_line hl = at(nl, at(ns, sec).line);
        append_range(o, nt + hl.off, hl.len);
        push_back(o, '\n');
        for (size_t g = 0; g < G; g++) {
            dl_group gr = at(gs, g);
            if (gr.sec != (uint32_t)sec) continue;
            dl_diff(ot, &ol, data(A) + gr.a0, gr.na, nt, &nl, data(B) + gr.b0, gr.nb, &runs, &v, &trace);
            uint32_t kept = 0, last_b = 0;
            for (size_t r = 0; r < len(runs); r++) { dl_run x = at(runs, r); kept += x.len; last_b = x.b + x.len; }
            uint32_t del = gr.na - kept, ins = gr.nb - kept;
            if (del == 0 && ins == 0) continue;
            st->retracted += del;
            st->added += ins;
            st->kept += kept;
            /* edited, when the added facts come after the kept ones and the retracted ones are few
               and have no twin; else rebuilt */
            int edit = kept == last_b && del <= 64;
            resize(covered, (size_t)gr.na + 1, 0);
            for (uint32_t i = 0; i < gr.na; i++) covered[i] = 0;
            for (size_t r = 0; r < len(runs); r++) {
                dl_run x = at(runs, r);
                for (uint32_t i = 0; i < x.len; i++) covered[x.a + i] = 1;
            }
            for (uint32_t i = 0; edit && i < gr.na; i++) {
                if (covered[i]) continue;
                dl_line li = at(ol, at(A, gr.a0 + i));
                for (uint32_t j = 0; j < gr.na; j++)
                    if (j != i && dl_same(ot, li, ot, at(ol, at(A, gr.a0 + j)))) { edit = 0; break; }
            }
            /* the reasoner's work, in us, as measured on the large page: a fact read and
               stored 3.6, a fact retracted 0.005 a fact it is among, a rebuild's copy of the
               facts held 0.85 each and its kept ones stored again 0.4 each */
            double ce = (double)del * (3.0 + 0.005 * gr.na) + 3.6 * ins;
            double cr = 0.85 * gr.na + 0.4 * kept + 3.6 * ins + (double)len(runs);
            if (edit && ce > cr) edit = 0;
            cost += edit ? ce : cr;
            put_s(o, edit ? "amb_edit(" : "amb_rebuild(");
            append_range(o, data(names) + gr.name_off, gr.name_len);
            put_s(o, ").\n");
            if (edit) {
                st->edits++;
                for (uint32_t i = 0; i < gr.na; i++)
                    if (!covered[i]) put_line(o, "-(", ot, at(ol, at(A, gr.a0 + i)));
                for (uint32_t j = kept; j < gr.nb; j++) put_line(o, "+(", nt, at(nl, at(B, gr.b0 + j)));
            } else {
                st->rebuilds++;
                uint32_t j = 0;
                for (size_t r = 0; r <= len(runs); r++) {
                    uint32_t until = gr.nb;
                    dl_run x = { 0, 0, 0 };
                    if (r < len(runs)) { x = at(runs, r); until = x.b; }
                    for (; j < until; j++) put_line(o, "+(", nt, at(nl, at(B, gr.b0 + j)));
                    if (r < len(runs)) {
                        char k[64];
                        snprintf(k, sizeof k, "amb_keep(%u,%u).\n", (unsigned)x.a, (unsigned)x.len);
                        put_s(o, k);
                        j = x.b + x.len;
                    }
                }
            }
        }
    }
    put_s(o, "amb_end.\n");
    st->cost_delta = cost;
    st->cost_whole = 4.9 * (double)st->new_facts;        /* a whole snapshot: 4.9 us a fact, read, checked and stored */
    return 1;
}

int amb_delta_write(FILE *out, const char *old, size_t old_n, const char *nw, size_t nw_n, amb_delta_stats *st) {
    vector<char> o;
    int r = dl_build(&o, "amb_delta.\n", old, old_n, nw, nw_n, st);
    if (r == 1) fwrite(data(o), 1, len(o), out);
    return r;
}

/* ---- the session's reasoner ---------------------------------------------------- */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* a file's bytes after what out holds: read in one call into room made by resize (a
   memset), where append_range would copy them a byte at a time */
static int read_file(const char *path, vector<char> *out) {
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    struct stat fs;
    size_t want = fstat(fileno(f), &fs) == 0 && fs.st_size > 0 ? (size_t)fs.st_size : 0;
    for (;;) {
        size_t base = len(*out), room = want ? want + 1 : 65536;
        resize(*out, base + room);
        size_t k = fread(data(*out) + base, 1, room, f);
        resize(*out, base + k);
        if (k < room) break;
        want = 0;
    }
    return 1;
}

static int write_file(const char *path, const char *p, size_t n) {
    own FILE *f = fopen(path, "wb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    return fwrite(p, 1, n, f) == n;
}

void amb_reasoner_init(amb_reasoner *r, const char *prog, const char *script, const char *held, const char *err) {
    memset(r, 0, sizeof *r);
    snprintf(r->prog, sizeof r->prog, "%s", prog);
    snprintf(r->script, sizeof r->script, "%s", script);
    snprintf(r->held, sizeof r->held, "%s", held);
    snprintf(r->err, sizeof r->err, "%s", err);
    r->to = -1;
    r->from = -1;
}

/* the reasoner gone: its pipes closed, its process waited for, how it ended kept */
static void reasoner_lost(amb_reasoner *r, int kill_it) {
    if (r->to >= 0) close(r->to);
    if (r->from >= 0) close(r->from);
    r->to = -1;
    r->from = -1;
    if (r->pid > 0) {
        if (kill_it) kill(r->pid, SIGKILL);
        int st = 0;
        if (waitpid(r->pid, &st, 0) == r->pid) ending_of(st, &r->end);
    }
    r->pid = 0;
    r->holds = 0;
    r->facts = 0;
}

static int reasoner_start(amb_reasoner *r, const amb_sandbox *sb) {
    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC) != 0) return -1;
    if (pipe2(out, O_CLOEXEC) != 0) { close(in[0]); close(in[1]); return -1; }
    int err = open(r->err, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    uint64_t nonce = 0;
    int rf = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (rf >= 0) { (void)!read(rf, &nonce, sizeof nonce); close(rf); }
    nonce ^= (uint64_t)getpid() << 32 ^ (uint64_t)(now_ms() * 1000);
    r->nonce = nonce >> 1;
    char nb[32], prog[1024], script[1024];
    snprintf(nb, sizeof nb, "%llu", (unsigned long long)r->nonce);
    snprintf(prog, sizeof prog, "%s", r->prog);
    snprintf(script, sizeof script, "%s", r->script);
    const char *argv[] = { prog, "-s", script, "--", "serve", nb, 0 };
    pid_t pid = fork();
    if (pid == 0) child_run(argv, in[0], out[1], err, sb);
    close(in[0]);
    close(out[1]);
    if (err >= 0) close(err);
    if (pid < 0) { close(in[1]); close(out[0]); return -1; }
    r->pid = pid;
    r->to = in[1];
    r->from = out[0];
    r->holds = 0;
    r->facts = 0;
    r->starts++;
    return 0;
}

/* bytes to the reasoner, SIGPIPE ignored: a reasoner that ended makes the write fail,
   where the signal would end the engine */
static int reasoner_send(amb_reasoner *r, const char *p, size_t n) {
    struct sigaction ign, old;
    sigemptyset(&ign.sa_mask);
    ign.sa_flags = 0;
    ign.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ign, &old);
    size_t done = 0;
    int ok = 1;
    while (done < n) {
        ssize_t k = write(r->to, p + done, n - done);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) { ok = 0; break; }
        done += (size_t)k;
    }
    sigaction(SIGPIPE, &old, 0);
    return ok ? 0 : -1;
}

/* the answer to the request sent: its text before amb_done(NONCE, Status) into ans,
   its status; -1 when the reasoner ended or did not answer in time */
static int reasoner_answer(amb_reasoner *r, vector<char> *ans, const amb_sandbox *sb) {
    char mark[64];
    int ml = snprintf(mark, sizeof mark, "amb_done(%llu, ", (unsigned long long)r->nonce);
    size_t scan = 0;
    double limit = sb && sb->timeout ? (double)sb->timeout * 1000.0 : 0;
    double t0 = now_ms();
    for (;;) {
        /* a line that begins with the mark ends the answer */
        while (scan < len(*ans)) {
            size_t e = scan;
            while (e < len(*ans) && at(*ans, e) != '\n') e++;
            if (e == len(*ans)) break;
            if (e - scan > (size_t)ml && memcmp(data(*ans) + scan, mark, (size_t)ml) == 0) {
                int st = atoi(data(*ans) + scan + ml);
                resize(*ans, scan, 0);
                return st;
            }
            scan = e + 1;
        }
        int wait = -1;
        if (limit > 0) {
            double left = limit - (now_ms() - t0);
            if (left <= 0) {
                reasoner_lost(r, 1);
                snprintf(r->end.why, sizeof r->end.why, "no answer in %llu s: the engine ended it", (unsigned long long)sb->timeout);
                return -1;
            }
            wait = (int)left + 1;
        }
        struct pollfd pf;
        pf.fd = r->from;
        pf.events = POLLIN;
        pf.revents = 0;
        int pr = poll(&pf, 1, wait);
        if (pr <= 0) continue;                  /* a signal, or the time: asked again */
        char buf[65536];
        ssize_t k = read(r->from, buf, sizeof buf);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) { reasoner_lost(r, 0); return -1; }
        append_range(ans, buf, (size_t)k);
    }
}

/* the facts of a snapshot text: its lines that are no header */
static uint64_t facts_of(const char *t, size_t n) {
    uint64_t c = 0;
    size_t i = 0;
    while (i < n) {
        size_t e = dl_eol(t, n, i);
        if (e > i && !(e - i > 13 && memcmp(t + i, "amb_snapshot(", 13) == 0)) c++;
        i = e + 1;
    }
    return c;
}

/* a request sent and its answer held(N) read: N, or -1 */
static int64_t reasoner_held(amb_reasoner *r, const char *head, const char *body, size_t bn, const char *tail, const amb_sandbox *sb) {
    if (reasoner_send(r, head, strlen(head)) != 0 || (bn && reasoner_send(r, body, bn) != 0) || reasoner_send(r, tail, strlen(tail)) != 0) {
        reasoner_lost(r, 0);
        return -1;
    }
    vector<char> ans;
    int st = reasoner_answer(r, &ans, sb);
    r->requests++;
    if (st != 0) return -1;
    push_back(ans, 0);
    long long n = -1;
    if (sscanf(data(ans), "held(%lld)", &n) != 1) return -1;
    return (int64_t)n;
}

int amb_reasoner_sync(amb_reasoner *r, const char *snap, const amb_sandbox *sb) {
    double t0 = now_ms();
    vector<char> nw;
    if (!read_file(snap, &nw)) return -1;
    uint64_t want = facts_of(data(nw), len(nw));
    if (r->pid <= 0 && reasoner_start(r, sb) != 0) return -1;
    int sent = 0;
    if (r->holds) {
        vector<char> old;
        if (read_file(r->held, &old)) {
            vector<char> req;
            amb_delta_stats st;
            int d = dl_build(&req, r->track ? "amb_delta(changes).\n" : "amb_delta.\n", data(old), len(old), data(nw), len(nw), &st);
            r->diff_ms = now_ms() - t0;
            if (d == 0) { r->sync_ms = now_ms() - t0; return 0; }
            r->retracted = 0;
            r->added = 0;
            r->kept = 0;
            if (d == 1 && st.cost_delta < st.cost_whole) {
                int64_t n = reasoner_held(r, "", data(req), len(req), "", sb);
                if (n >= 0 && (uint64_t)n == want) {
                    r->deltas++;
                    r->last = 2;
                    r->edits += st.edits;
                    r->rebuilds += st.rebuilds;
                    r->retracted = st.retracted;
                    r->added = st.added;
                    r->kept = st.kept;
                    sent = 1;
                }
            }
        }
    }
    if (!sent) {
        if (r->pid <= 0 && reasoner_start(r, sb) != 0) return -1;
        int64_t n = reasoner_held(r, "amb_load.\n", data(nw), len(nw), "amb_end.\n", sb);
        if (n < 0 || (uint64_t)n != want) { r->holds = 0; return -1; }
        r->loads++;
        r->last = 1;
        r->retracted = 0;
        r->added = want;
        r->kept = 0;
    }
    r->facts = want;
    r->holds = write_file(r->held, data(nw), len(nw));
    r->sync_ms = now_ms() - t0;
    return 0;
}

/* a goal as a quoted atom: ' and \ escaped, control characters in hex */
static void put_qatom(vector<char> *o, const char *s) {
    push_back(o, '\'');
    for (size_t i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\'' || c == '\\') { push_back(o, '\\'); push_back(o, (char)c); }
        else if (c < 0x20 || c == 0x7f) { char h[8]; snprintf(h, sizeof h, "\\x%x\\", (unsigned)c); put_s(o, h); }
        else push_back(o, (char)c);
    }
    push_back(o, '\'');
}

int amb_reasoner_ask(amb_reasoner *r, const char *cmd, const char *goal, const char *out, const amb_sandbox *sb) {
    double t0 = now_ms();
    struct stat es;
    r->err_mark = stat(r->err, &es) == 0 ? (long)es.st_size : 0;
    if (r->pid <= 0) return -1;
    vector<char> req;
    put_s(&req, cmd);
    if (goal) { push_back(req, '('); put_qatom(&req, goal); push_back(req, ')'); }
    put_s(&req, ".\n");
    if (reasoner_send(r, data(req), len(req)) != 0) { reasoner_lost(r, 0); return -1; }
    vector<char> ans;
    int st = reasoner_answer(r, &ans, sb);
    r->requests++;
    if (out) write_file(out, data(ans), len(ans));
    r->ask_ms = now_ms() - t0;
    return st;
}

void amb_reasoner_stop(amb_reasoner *r) {
    if (r->pid > 0) {
        reasoner_send(r, "quit.\n", 6);
        if (r->to >= 0) close(r->to);
        r->to = -1;
        reasoner_lost(r, 0);
    }
}

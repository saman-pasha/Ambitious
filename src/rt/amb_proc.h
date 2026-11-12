/* src/rt/amb_proc.h -- the processes the engine runs (src/rt/amb_proc.c): a reasoner
   run once over a snapshot file, or one kept for a session that holds a snapshot and
   is sent what changed in it; each in a sandbox. Every struct here holds no pointer,
   so a session keeps them as values. */
#ifndef AMBITIOUS_AMB_PROC_H
#define AMBITIOUS_AMB_PROC_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* the sandbox a program runs in: limits, no network, a syscall filter, and the one
   directory it may write in */
typedef struct amb_sandbox {
    uint64_t mem;            /* its address space, bytes (RLIMIT_AS) */
    uint64_t cpu;            /* its CPU time, seconds (RLIMIT_CPU) */
    uint64_t files;          /* the files it may hold open (RLIMIT_NOFILE) */
    uint64_t fsize;          /* the largest file it may write, bytes (RLIMIT_FSIZE) */
    uint64_t timeout;        /* a session's answer, waited for at most this many seconds */
    int on;                  /* 0: none, the program runs as the engine does */
    char dir[1024];          /* its working directory, the only one it may write in */
} amb_sandbox;

/* how a process ended, in the engine's words */
typedef struct amb_ending {
    int ended;               /* 1 once it has ended */
    int exited;              /* 1: it exited with code; 0: the signal code ended it */
    int code;
    char why[240];           /* "exited 2", "killed by its syscall filter (SIGSYS)", ... */
} amb_ending;

/* the sandbox as AMB_SANDBOX (0: none), AMB_REASON_MEM (bytes, or with K, M or G),
   AMB_REASON_CPU and AMB_REASON_TIMEOUT (seconds), AMB_REASON_FILES and
   AMB_REASON_FSIZE set it, else its defaults; dir its working directory, made 0700 */
void amb_sandbox_init(amb_sandbox *sb, const char *dir);
/* what the sandbox holds on this machine, a line each into out: each limit, the
   network, file writes and the syscall filter, as a child given it finds them */
int amb_sandbox_report(const amb_sandbox *sb, FILE *out);

/* a program run to its end (as amb_run, include/amb.pl.h), in the sandbox sb (0: none);
   how it ended into how (or 0). Its exit status, or -1 when it did not exit */
int amb_run_in(const char *const argv[], const char *out_path, const char *err_path, const amb_sandbox *sb, amb_ending *how);

/* a reasoner kept for a session: cocolog over reason.pl serve, its requests and answers
   over two pipes. It holds a snapshot: the first it is given whole, each one after as
   a delta from the one it holds (amb_delta_write) */
typedef struct amb_reasoner {
    int pid;                 /* its process; 0 when none runs */
    int to, from;            /* its standard input and output */
    uint64_t nonce;          /* its answers end amb_done(NONCE, Status) */
    uint64_t facts;          /* the facts it holds */
    uint32_t starts, requests, loads, deltas, edits, rebuilds;
    uint32_t last;           /* the last snapshot that changed came: 1 whole, 2 as a delta */
    uint64_t retracted, added, kept;   /* its facts */
    long err_mark;           /* its errors' file's size before the last request */
    double sync_ms, ask_ms;  /* the last snapshot's time to reach it, and the last answer's */
    double diff_ms;          /* of sync_ms, the engine's: the two snapshots read and compared */
    amb_ending end;          /* how it ended, when it has */
    char prog[1024];         /* cocolog */
    char script[1024];       /* prolog/amb/reason.pl */
    char held[1024];         /* a copy of the snapshot it holds */
    char err[1024];          /* its standard error */
    int holds;               /* 1 when held is what it holds */
    int track;               /* 1: the next delta's facts kept, for a `changes' request */
} amb_reasoner;

/* the reasoner made ready: nothing runs until the first request */
void amb_reasoner_init(amb_reasoner *r, const char *prog, const char *script, const char *held, const char *err);
/* the reasoner made to hold the snapshot in the file snap: nothing sent when it holds
   it already, a delta when it holds another, else the whole; started (in sb) when none
   runs. 0 done, -1 not */
int amb_reasoner_sync(amb_reasoner *r, const char *snap, const amb_sandbox *sb);
/* a request (check, advise, changes; query and why with their goal) answered over the
   snapshot held, the answer into the file out: the reasoner's status, -1 when it
   ended (r->end says how) */
int amb_reasoner_ask(amb_reasoner *r, const char *cmd, const char *goal, const char *out, const amb_sandbox *sb);
/* the reasoner told to quit and waited for */
void amb_reasoner_stop(amb_reasoner *r);

/* what a delta holds */
typedef struct amb_delta_stats {
    uint64_t old_facts, new_facts;
    uint64_t retracted, added, kept;
    uint32_t edits, rebuilds;
    double cost_delta, cost_whole;     /* the reasoner's work each way, estimated in us */
} amb_delta_stats;

/* the delta from the snapshot text old to the snapshot text nw, written to out: each
   section's header, then each predicate that changed, as prolog/amb/reasoner.pl reads
   it (amb_edit or amb_rebuild), so that the facts held come out in nw's order. 1 when
   written, 0 when the texts are the same, 2 when no delta fits (the sections differ):
   the whole snapshot is to be sent */
int amb_delta_write(FILE *out, const char *old, size_t old_n, const char *nw, size_t nw_n, amb_delta_stats *st);
#endif

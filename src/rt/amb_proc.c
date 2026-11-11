/* src/rt/amb_proc.c -- running the reasoner: a program, its standard input
   /dev/null, its standard output to a file, its standard error to another (or
   the engine's own), waited for. No shell runs, so no argument is quoted or
   expanded: the reasoner is reached only by the arguments the engine gives. */
#include <amb.pl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

int amb_run(const char *const argv[], const char *out_path, const char *err_path) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int in = open("/dev/null", O_RDONLY);
        int out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (in < 0 || out < 0) _exit(126);
        dup2(in, 0);
        dup2(out, 1);
        if (err_path) {
            int err = open(err_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (err < 0) _exit(126);
            dup2(err, 2);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
}

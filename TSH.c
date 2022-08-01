#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

typedef struct IO IO;
typedef struct Program Program;

enum {
    USE_FILE = 1,
    USE_PIPE = 2
};

struct IO {
    union {
        struct {
            int usefd;
            int closefd;
        } p;
        struct {
            char *name;
            int flags;
        } f;
    } io;
    int type;
};

struct Program {
    char seqop;
    IO input;
    IO output;
    char *args[128];
};

void parseseq(char *);
void parsepipe(char *, char);
void parseredirect(Program *, char *);
void closepipe(IO *);
void parseargs(Program *, char *);
void runcmd(Program *);
void usecd(char **);
void redirect(IO *, int);

int
main(int argc, char *argv[])
{
    char buf[8192];

    while (1) {
        printf("$ ");
        if (fgets(buf, 8192, stdin) == NULL) {
            printf("\nClosing... \n");
            break;
        }

        parseseq(buf);
    }

    exit(0);
}

// Parses command sequences separated by ; and &
void
parseseq(char *cmd)
{
    char seqop;
    char buf[strlen(cmd) + 1];
    char *currseq, *lastseq;

    strcpy(buf, cmd);

    // Iterate over each sequence in the command line
    currseq = strtok_r(buf, ";&\n", &lastseq);
    while (1) {
        if (currseq == NULL) {
            break;
        }

        // Locate sequence operator using original command line as a reference
        seqop = cmd[currseq - buf + strlen(currseq)];

        parsepipe(currseq, seqop);

        // Continue to the next sequence
        currseq = strtok_r(NULL, ";&\n", &lastseq);
    }
}

// Joins programs separated by | via pipe file descriptors
void
parsepipe(char *seq, char op)
{
    int cmdcount;
    int pfd[2];
    char *currpipe;
    char *lastpipe;
    Program program;

    cmdcount = 0;
    currpipe = strtok_r(seq, "|", &lastpipe);
    while (1) {
        if (currpipe == NULL) {
            break;
        }

        memset(&program, 0, sizeof(Program));
        program.seqop = op;

        if (cmdcount == 1) {
            cmdcount = 0;
            program.input.io.p.usefd = pfd[0];
            program.input.io.p.closefd = pfd[1];
            program.input.type = USE_PIPE;
        }

        /**
         * The below conditional expressions may seem redundant but have a purpose
         * The final argument value of strtok_r can vary on different systems per man pages
         * Mac OS X sets the final argument to NULL once a string has been tokenized
         * Tux was setting the final argument to "\0" which bypassed the first expression
         */
        if (lastpipe != NULL && strlen(lastpipe) > 0) {
            pipe(pfd);
            cmdcount++;
            program.output.io.p.usefd = pfd[1];
            program.output.io.p.closefd = pfd[0];
            program.output.type = USE_PIPE;
        }

        parseredirect(&program, currpipe);
        parseargs(&program, currpipe);
        runcmd(&program);

        currpipe = strtok_r(NULL, "|", &lastpipe);
    }
}

// Finds file names in a command string for I/O redirection
void
parseredirect(Program *progptr, char *cmd)
{
    int i, len, flags;
    char *match;
    IO *ioptr;

    if ((match = strpbrk(cmd, "<>")) == NULL) {
        return;
    }

    len = strlen(cmd);
    for (i = match - cmd; i < len; i++) {
        if (cmd[i] == '<') {
            cmd[i] = '\0';
            ioptr = &progptr->input;
            closepipe(ioptr);
            ioptr->io.f.name = NULL;
            ioptr->type = USE_FILE;
            ioptr->io.f.flags = O_RDONLY;
        }

        else if (cmd[i] == '>') {
            cmd[i] = '\0';
            ioptr = &progptr->output;
            closepipe(ioptr);
            ioptr->io.f.name = NULL;
            ioptr->type = USE_FILE;

            if (cmd[i + 1] == '>') {
                cmd[++i] = '\0';
                ioptr->io.f.flags = O_CREAT | O_WRONLY | O_APPEND;
            }

            else {
                ioptr->io.f.flags = O_CREAT | O_WRONLY | O_TRUNC;
            }
        }

        else if (isspace(cmd[i])) {
            cmd[i] = '\0';
        }

        else {
            if (ioptr->io.f.name == NULL) {
                ioptr->io.f.name = cmd + i;
            }
        }
    }
}

// Closes active side of pipe being used for input or output based on parameter
void
closepipe(IO *ioptr)
{
    if (ioptr->type == USE_PIPE) {
        close(ioptr->io.p.usefd);
    }
}

// Parses program and program arguments from command string
void
parseargs(Program *progptr, char *cmd)
{
    int i;
    char *last;

    i = 0;
    progptr->args[i] = strtok_r(cmd, " \t", &last);
    while (1) {
        if (progptr->args[i] == NULL) {
            break;
        }
        progptr->args[++i] = strtok_r(NULL, " \t", &last);
    }
}

// Runs built-in command or program specified by user
void
runcmd(Program *progptr)
{
    int status;
    pid_t pid;

    // Handles commands with only white space and the bash null command :
    if (progptr->args[0] != NULL && strcmp(progptr->args[0], ":") != 0) {
        if (strcmp(progptr->args[0], "cd") == 0) {
            usecd(progptr->args);
        }

        else {
            if ((pid = fork()) == 0) {
                redirect(&progptr->input, 0);
                redirect(&progptr->output, 1);
                execvp(progptr->args[0], progptr->args);
                perror("exec");
                exit(1);
            }

            else if (progptr->seqop != '&') {
                // Handles edge case where backgrounded and non-backgrounded commands finish at same time
                while (pid != wait(&status));
            }

            else {
                printf("Backgrounding command %s - %d\n", progptr->args[0], pid);
            }
        }
    }

    closepipe(&progptr->input);
    closepipe(&progptr->output);
}

// Runs built-in cd command
void
usecd(char **args)
{
    int n;
    char *path;

    // Changes to root directory if cd has no arguments
    if (args[1] == NULL) {
        n = chdir(getenv("HOME"));
    }

    // Returns if any more than 1 arg to cd is provided
    else if (args[2] != NULL) {
        fprintf(stderr, "cd: too many arguments\n");
        return;
    }

    // Supports cd ~ , cd ~/directorypath
    else if (args[1][0] == '~') {
        path = malloc(1 * (strlen(getenv("HOME")) + strlen(args[1])));
        snprintf(path, strlen(getenv("HOME")) + strlen(args[1]), "%s%s", getenv("HOME"), args[1] + 1);
        n = chdir(path);
        free(path);
    }

    // Supports cd . , cd .. , cd /abspath , cd relpath
    else {
        n = chdir(args[1]);
    }

    if (n == -1) {
        perror("chdir");
    }
}

// Handles I/O redirection to file or pipe
void
redirect(IO *ioptr, int fd)
{
    switch (ioptr->type) {
        case USE_FILE:
            close(fd);
            if (open(ioptr->io.f.name, ioptr->io.f.flags, 0644) == -1) {
                perror("open");
                exit(1);
            }
            break;
        case USE_PIPE:
            close(ioptr->io.p.closefd);
            close(fd);
            dup(ioptr->io.p.usefd);
            break;
    }
}
/*
 * esh - the 'extensible' shell.
 *
 * Developed by Godmar Back for CS 3214 Fall 2009
 * Virginia Tech.
 * 
 * $Id: esh.h,v 1.4 2011/03/29 15:46:28 cs3214 Exp $
 */

#include <stdbool.h>
#include <obstack.h>
#include <stdlib.h>
#include <termios.h>
#include "list.h"

/* Forward declarations. */
struct esh_command;
struct esh_pipeline;
struct esh_command_line;
struct list jobsList;
struct termios * shell_state;
pid_t shell_pgpr;

/*
 * A esh_shell object allows plugins to access services and information. 
 * The shell object should support the following operations.
 */
struct esh_shell {

    /* Return the list of current jobs */
    struct list/* <esh_pipeline> */ * (* get_jobs) (void);

    /* Return job corresponding to jid */
    struct esh_pipeline * (* get_job_from_jid)(int jid);

    /* Return job corresponding to pgrp */
    struct esh_pipeline * (* get_job_from_pgrp)(pid_t pgrp);

    /* Return process corresponding to pid */
    struct esh_command * (* get_cmd_from_pid)(pid_t pid);

    /* Build a prompt.  Memory must be malloc'd */
    char * (* build_prompt) (void);

    /* Read an input line */
    char * (* readline) (const char *prompt);

    /* Parse command line */
    struct esh_command_line * (* parse_command_line) (char *);
};

/* 
 * Modules must define a esh_plugin instance named 'esh_module.'
 * Each of the following members is optional.
 * esh will call its 'init' functions upon successful load.
 *
 * For binary compatibility, do not change the order of these fields.
 */
struct esh_plugin {
    struct list_elem elem;    /* Link element */

    /* an integer number denoting the relative rank of the plugin
     * Plugins are notified of events in increasing rank. */
    int rank;

    /* Initialize plugin and pass shell object */
    bool (* init)(struct esh_shell *);

    /* The return value of the following three functions indicates
     * whether the plugin wants processing to stop.
     * true - indicates processing should stop.
     * false - indicates processing should continue.
     */
    /* The command line the user entered.
     * A plugin may change it. */
    bool (* process_raw_cmdline)(char **);

    /* A given pipeline of commands 
     * A plugin may change it. */
    bool (* process_pipeline)(struct esh_pipeline *);

    /* If the command is a built-in provided by a plugin, execute the
     * command and return true. */
    bool (* process_builtin)(struct esh_command *);

    /* Manufacture part of a prompt.  Memory must be allocated via malloc(). 
     * If no plugin implements this, the shell will provide a default prompt. */
    char * (* make_prompt)(void);

    /* The process or processes that are part of a new pipeline
     * have been forked.  
     * The jid and pgid fields of the pipeline and the pid fields
     * of all commands in the pipeline are set.
     * SIGCHLD is blocked.
     */
    void (* pipeline_forked)(struct esh_pipeline *);

    /* Notify the plugin about a child's status change.
     * 'waitstatus' is the value returned by waitpid(2) 
     *
     * May be called from SIGCHLD handler.
     * The status of the associated pipeline has not yet been
     * updated.
     * */
    bool (* command_status_change)(struct esh_command *, int waitstatus);

    /* Add additional fields here if needed. */
};

/* A command line may contain multiple pipelines. */
struct esh_command_line {
    struct list/* <esh_pipeline> */ pipes;        /* List of pipelines */

    /* Add additional fields here if needed. */
};

enum job_status  { 
    FOREGROUND = 0,     /* job is running in foreground.  Only one job can be
                       in the foreground state. */
    BACKGROUND = 1,     /* job is running in background */
    STOPPED = 2,        /* job is stopped via SIGSTOP */
    NEEDSTERMINAL = 3,  /* job is stopped because it was a background job
                       and requires exclusive terminal access */
};

/* A pipeline is a list of one or more commands. 
 * For the purposes of job control, a pipeline forms one job.
 */
struct esh_pipeline {
    struct list/* <esh_command> */ commands;    /* List of commands */
    char *iored_input;       /* If non-NULL, first command should read from
                                file 'iored_input' */
    char *iored_output;      /* If non-NULL, last command should write to
                                file 'iored_output' */
    bool append_to_output;   /* True if user typed >> to append */
    bool bg_job;             /* True if user entered & */
    struct list_elem elem;   /* Link element. */

    int     jid;             /* Job id. */
    pid_t   pgrp;            /* Process group. */
    enum job_status status;  /* Job status. */ 
    struct termios saved_tty_state;  /* The state of the terminal when this job was 
                                        stopped after having been in foreground */

    int notified;	     /*1 if we have already notified the user that the job has stopped*/
    /* Add additional fields here if needed. */
};

struct esh_job {
	struct list /* <esh command> */ commands; 	/* List of commands */
	bool bg_job;	/*true if user entered & */
	enum job_status status;	/*Job Status*/
	int jid;	/*Job id*/
	pid_t pgrp; 	/*process group*/
	struct list_elem elem;	/* Link element */
};

/* A command is part of a pipeline. */
struct esh_command {
    char **argv;             /* NULL terminated array of pointers to words
                                making up this command. */
    char *iored_input;       /* If non-NULL, command should read from
                                file 'iored_input' */
    char *iored_output;      /* If non-NULL, command should write to
                                file 'iored_output' */
    bool append_to_output;   /* True if user typed >> to append */
    struct list_elem elem;   /* Link element to link commands in pipeline. */

    pid_t   pid;             /* Process id. */
    struct esh_pipeline * pipeline; 
                              /* The pipeline of which this job is a part. */
    int status;		/*status of process*/
    int stop;		/*1 if process is stopped by SIGSTOP*/ 
    int complete;	/*1 if process has completed*/
    /* Add additional fields here if needed. */
};

/** ----------------------------------------------------------- */

/* Create new command structure and initialize it */
struct esh_command * esh_command_create(char ** argv, 
                   char *iored_input, 
                   char *iored_output, 
                   bool append_to_output);

/* Create a new pipeline containing only one command */
struct esh_pipeline * esh_pipeline_create(struct esh_command *cmd);

/* Complete a pipe's setup by copying I/O redirection information
 * from first and last command */
void esh_pipeline_finish(struct esh_pipeline *pipe);

/* Create an empty command line */
struct esh_command_line * esh_command_line_create_empty(void);

/* Create a command line with a single pipeline */
struct esh_command_line * esh_command_line_create(struct esh_pipeline *pipe);

/* Deallocation functions */
void esh_command_line_free(struct esh_command_line *);
void esh_pipeline_free(struct esh_pipeline *);
void esh_command_free(struct esh_command *);

/* Print functions */
void esh_command_print(struct esh_command *cmd);
void esh_pipeline_print(struct esh_pipeline *pipe);
void esh_command_line_print(struct esh_command_line *line);

/* Parse a command line.  Implemented in esh-grammar.y */
struct esh_command_line * esh_parse_command_line(char * line);

/* Load plugins from directory dir */
void esh_plugin_load_from_directory(char *dirname);

/* Initialize loaded plugins */
void esh_plugin_initialize(struct esh_shell *shell);

/* List of loaded plugins */
extern struct list esh_plugin_list;

/* Determines if a pipeline is a builtin command */
int is_builtin_command(struct list_elem * pipe);

/*calls exec on an esh_command*/
void launch_command(struct esh_command * command, int shell_is_interactive);

/*Marks the status of each process in a job while it is running */
int mark_process_status(pid_t pid, int status, struct esh_pipeline * pipe);

void do_job_notification(int pointless_var);

void wait_for_job(struct esh_pipeline * pipe);

int job_is_stopped(struct esh_pipeline * pipe);

int job_is_complete(struct esh_pipeline * pipe);

void put_job_in_background(struct esh_pipeline * pipe, int cont);

void put_job_in_foreground(struct esh_pipeline * pipe, int cont, pid_t shellGroup, struct termios * shellState);

void continue_job(struct esh_pipeline * pipe, bool bg, pid_t shell_group, struct termios * shell_state);

void mark_job_as_running(struct esh_pipeline * pipe);

void update_status(struct list * jobList);
	
void print_job_name(struct esh_pipeline * job);

void foreground(int jid, struct list * jobList, pid_t shell_group, struct termios * shell_state);

void give_terminal_to(pid_t pgrp, struct termios *pg_tty_state);

void launch_job (struct esh_pipeline * job, bool bg, int shell_is_interactive, pid_t shell_pgrp, struct termios * shell_state);

struct esh_pipeline * findJob(int pgrp);

void handler(int sig);

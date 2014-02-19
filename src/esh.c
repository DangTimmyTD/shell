/*
 * esh - the 'pluggable' shell.
 *
 * Developed by Godmar Back for CS 3214 Fall 2009
 * Virginia Tech.
 */
#include <stdio.h>
#include <readline/readline.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#include "esh.h"
#include "esh-sys-utils.h"

static void
usage(char *progname)
{
    printf("Usage: %s -h\n"
        " -h            print this help\n"
        " -p  plugindir directory from which to load plug-ins\n",
        progname);

    exit(EXIT_SUCCESS);
}

/* Build a prompt by assembling fragments from loaded plugins that 
 * implement 'make_prompt.'
 *
 * This function demonstrates how to iterate over all loaded plugins.
 */
static char *
build_prompt_from_plugins(void)
{
    char *prompt = NULL;
    struct list_elem * e = list_begin(&esh_plugin_list);

    for (; e != list_end(&esh_plugin_list); e = list_next(e)) {
        struct esh_plugin *plugin = list_entry(e, struct esh_plugin, elem);

        if (plugin->make_prompt == NULL)
            continue;

        /* append prompt fragment created by plug-in */
        char * p = plugin->make_prompt();
        if (prompt == NULL) {
            prompt = p;
        } else {
            prompt = realloc(prompt, strlen(prompt) + strlen(p) + 1);
            strcat(prompt, p);
            free(p);
        }
    }

    /* default prompt */
    if (prompt == NULL)
        prompt = strdup("esh> ");

    return prompt;
}

/* The shell object plugins use.
 * Some methods are set to defaults.
 */
struct esh_shell shell =
{
    .build_prompt = build_prompt_from_plugins,
    .readline = readline,       /* GNU readline(3) */ 
    .parse_command_line = esh_parse_command_line /* Default parser */
};

/**
 *  * Assign ownership of ther terminal to process group
 *   * pgrp, restoring its terminal state if provided.
 *    *
 *     * Before printing a new prompt, the shell should
 *      * invoke this function with its own process group
 *       * id (obtained on startup via getpgrp()) and a
 *        * sane terminal state (obtained on startup via
 *         * esh_sys_tty_init()).
 *          */

void give_terminal_to(pid_t pgrp, struct termios *pg_tty_state)
{
    esh_signal_block(SIGTTOU);
    int rc = tcsetpgrp(esh_sys_tty_getfd(), pgrp);
    if (rc == -1)
        esh_sys_fatal_error("tcsetpgrp: ");

    if (pg_tty_state)
        esh_sys_tty_restore(pg_tty_state);
    esh_signal_unblock(SIGTTOU);
}


/* Determines if a pipeline is a built in command or executable files
 *
 */

int is_builtin_command(struct list_elem * pipe)
{
	struct esh_pipeline * pipeline = list_entry(pipe, struct esh_pipeline, elem);
        struct list_elem * command = list_begin(&pipeline->commands);
	char * cmd =  &(*list_entry(command, struct esh_command, elem)->argv[0]);
	if (strcmp(cmd, "jobs") == 0){
		return 1;
        }
	if (strcmp(cmd, "fg") == 0){
                return 1;
        }
	if (strcmp(cmd, "bg") == 0){
                return 1;
        } 
	if (strcmp(cmd, "stop") == 0){
                return 1;
        } 
	if (strcmp(cmd, "kill") == 0){
                return 1;
        }
	if(strcmp(cmd, "quit_shell") == 0){
		return 1;
	} 
	return 0;
}

/*Launches a command by calling execvp. This is called after we fork within our child process*/
void launch_command(struct esh_command * command, int shell_is_interactive){
	if(shell_is_interactive){
		pid_t pid = getpid();
		pid_t pgrp = getpgid(pid);
		bool bg = command->pipeline->bg_job;
		
               
               

		if(command->pipeline->pgrp == 0){
			setpgid(0, 0);
			command->pipeline->pgrp = pgrp;
		}
	       
		if(setpgid(pid, command->pipeline->pgrp) == -1){
			setpgid(0, 0);
			command->pipeline->pgrp = pgrp;
		} 
		

		if(!bg){
			give_terminal_to(command->pipeline->pgrp, &command->pipeline->saved_tty_state);
		}
	}

	execvp(command->argv[0], command->argv);
	perror("execvp");
	exit(1);
	
}


/*
 * Forks a new process and calls launch command to start a job. 
 * Sets the process group id and pid for the child process in our jobs list 
 */
void launch_job (struct esh_pipeline * job, bool bg, int shell_is_interactive, pid_t shell_pgrp, struct termios * shell_state){
	struct list_elem * command = list_begin(&job->commands);
	job->pgrp = 0;
	for (; command != list_end(&job->commands); command = list_next(command)){
		struct esh_command * cmd = list_entry(command, struct esh_command, elem);
		cmd->complete=0;
		cmd->stop = 0;
		/*Fork a child process*/
		int pid = fork();
		if (pid == 0){

			signal(SIGTSTP, SIG_DFL);
			signal(SIGINT, SIG_DFL);
			/*Child process*/
			launch_command(cmd, shell_is_interactive);
		}
		else if (pid < 0){
			/*Fork Failed*/
			perror("fork");
			exit(1);
		}
		else{
			/*Parent Process*/
			cmd->pid = pid;
			if (shell_is_interactive){
				if(!job->pgrp){
					job->pgrp = pid;
				}
				setpgid(pid, job->pgrp);
			}
			printf("[%d] %d \n", job->jid, job->pgrp);
		}

		if(!shell_is_interactive){
			wait_for_job(job);
		}

		else if(!bg){
			put_job_in_foreground(job, 0, shell_pgrp, shell_state);
		}
		else{
			put_job_in_background(job, 0);
		}
	}
}

/*Prints a full pipeline's name out on one line*/
void print_job_name(struct esh_pipeline * job){
	struct list_elem * cmdLink = list_begin(&job->commands);
	for (; cmdLink != list_end(&job->commands); cmdLink = list_next(cmdLink)){
		struct esh_command * cmd = list_entry(cmdLink, struct esh_command, elem);
		int i = 0;
		while(cmd->argv[i]){
			printf("%s ", cmd->argv[i]);
			i++;
		}

		if(list_next(cmdLink) != list_end(&job->commands)){
			printf(" | ");
		}

	}
	printf("\n");
}


/*called within a continual while loop to update the status of each process in a jobs*/
int mark_process_status(pid_t pid, int status, struct esh_pipeline * pipe){
	if (pid > 0){
		struct list_elem * commandLink = list_begin(&(pipe->commands));
		for (; commandLink != list_end(&(pipe->commands)); commandLink = list_next(commandLink)){
			struct esh_command * command = list_entry(commandLink, struct esh_command, elem);
			if (command->pid == pid){
				command->status = status;
				if (WIFSTOPPED (status)){
					
					command->stop = 1;
				}
				else{
					
					command->complete = 1;				
				}
				return 0;
			}		
		}
		// No child matches pid
		return -1;
	}
	else if (pid == 0){
		// No child ready to update
		return -1;
	}
	else{
		// No more child processes to mark
		//perror("waitpid");
		return -1;
	}
	
}


/*return 1 if the job has been stopped, returns 0 otherwise*/
int job_is_stopped (struct esh_pipeline * pipe){
	struct list_elem * commandLink = list_begin(&pipe->commands);
	for (; commandLink != list_end(&pipe->commands); commandLink = list_next(commandLink)){
		struct esh_command * command = list_entry(commandLink, struct esh_command, elem);
		
		if (!command->stop == 1 && !command->complete == 1){
			return 0;
		}
	}
	return 1;

}

/*returns 1 if all processes in a job are complete*/
int job_is_complete (struct esh_pipeline * pipe){
        struct list_elem * commandLink = list_begin(&pipe->commands);
        for (; commandLink != list_end(&pipe->commands); commandLink = list_next(commandLink)){
                struct esh_command * command = list_entry(commandLink, struct esh_command, elem);
                if (command->complete != 1){
			return 0;
                }
        }
        return 1;
}

/*called before each prompt from our shell. updates the status of all jobs in the job list*/
void update_status(struct list * jobList){
	int status;
	pid_t pid;
		
	struct list_elem * jobLink = list_begin(jobList);	
	for (; jobLink != list_end(jobList); jobLink = list_next(jobLink)){
		struct esh_pipeline * job = list_entry(jobLink, struct esh_pipeline, elem);
		
		do
			pid = waitpid(WAIT_ANY, &status, WUNTRACED|WNOHANG);
		while (!mark_process_status(pid, status, job));
		
		if(job_is_stopped (job) && !job_is_complete(job)){
			job->status = STOPPED;
			printf("[%d] Stopped", job->jid);
			print_job_name(job);
			printf("\n");

		}
		
	}
}

/*Notifies the user if a particular job has completed*/
void do_job_notification(int pointless_var){
	struct list_elem * pipe = list_begin(&jobsList);

	update_status(&jobsList);
	
	for(; pipe != list_end(&jobsList); pipe = list_next(pipe)){
		struct esh_pipeline * job = list_entry(pipe, struct esh_pipeline, elem);

		if(job_is_complete(job)){
			printf("[%d]  DONE          ", job->jid);
			print_job_name(job);
			printf("\n");	
			list_remove(pipe);
			esh_pipeline_free(job);
		}
		else if(job_is_stopped(job) && !job->notified){
			job->notified = 1;
		}
	}
	
}

/*Waits for wach process of a job to complete*/
void wait_for_job(struct esh_pipeline * pipe){
	int status;
	pid_t pid;
	
	do
		pid = waitpid(WAIT_ANY, &status, WUNTRACED);
	while (!mark_process_status(pid, status, pipe)
		&& !job_is_stopped (pipe)
		&& !job_is_complete (pipe));
}
/* put a job in the foreground */
void put_job_in_foreground(struct esh_pipeline * pipe, int cont, pid_t shellGroup, struct termios * shellState){
	print_job_name(pipe);
	give_terminal_to(pipe->pgrp, &pipe->saved_tty_state);

	pipe->status = FOREGROUND;
	pipe->bg_job = false;	

	if(cont){
		esh_sys_tty_restore(&pipe->saved_tty_state);
		if(kill (- pipe->pgrp, SIGCONT) < 0){
			perror("kill (SIGCONT)");
		}
	}
	
	wait_for_job(pipe);
	if (!job_is_complete(pipe)){
		pipe->status = STOPPED;
		esh_sys_tty_save(&pipe->saved_tty_state);
	}
	else{
		list_remove(&pipe->elem);
		esh_pipeline_free(pipe);
	}
	
	give_terminal_to(shellGroup, shellState);
}

/*put a job in the background*/
void put_job_in_background(struct esh_pipeline * pipe, int cont){
	pipe->bg_job = true;
	if(cont){
		if(kill (- pipe->pgrp, SIGCONT) < 0){
			perror("kill (SIGCONT)");
		}
		else{
			struct list_elem * commandLink = list_begin(&pipe->commands);
			for (; commandLink != list_end(&pipe->commands); commandLink = list_next(commandLink)){
				struct esh_command * command = list_entry(commandLink, struct esh_command, elem);
				command->stop = 0;
			}
			pipe->status = BACKGROUND;
		}
	}
	
}
/*mark a job as running*/
void mark_job_as_running(struct esh_pipeline * pipe){
	struct list_elem * commandLink = list_begin(&pipe->commands);
	for (; commandLink != list_end(&pipe->commands); commandLink = list_next(commandLink)){
		struct esh_command * command = list_entry(commandLink, struct esh_command, elem);
		command->stop = 0;
	}
}
/*set a job's status as running*/
void continue_job(struct esh_pipeline * pipe, bool bg, pid_t shell_group, struct termios * shell_state){
	mark_job_as_running(pipe);
	if(!bg){
		put_job_in_foreground(pipe, 1, shell_group, shell_state);
	}
	else{
		put_job_in_background(pipe, 1);
	}
}

/*put job in foreground*/
void foreground(int jid, struct list * jobList, pid_t shell_group, struct termios * shell_state){
	struct list_elem * jobLink = list_begin(jobList);
	int found = 0;	
	for (; jobLink != list_end(jobList); jobLink = list_next(jobLink)){
		struct esh_pipeline * job = list_entry(jobLink, struct esh_pipeline, elem);
		if(job->jid == jid){
			found = 1;

			switch(job->status){
				case FOREGROUND: break;
				case BACKGROUND: put_job_in_foreground(job, 0, shell_group, shell_state); break;
				case STOPPED: continue_job(job, false, shell_group, shell_state); break;
				case NEEDSTERMINAL: continue_job(job, false, shell_group, shell_state); break;
			}
		}
	}
	if(!found){
		printf("No job mathcing that ID\n");
	}
}

/* find job command from process group*/
struct esh_pipeline * findJob(int pgrp){

	struct list_elem * jobLink = list_begin(&jobsList);
	for (; jobLink != list_end(&jobsList); jobLink = list_next(jobLink)){
		struct esh_pipeline * job = list_entry(jobLink, struct esh_pipeline, elem);
		if (job->pgrp == pgrp){
			return job;
		}
	}
	return NULL;
	

}
/*singal handler*/
pid_t shell_pgrp;

void handler(int sig){
	int status;
	pid_t pid;
 	printf("DErp");	
	while ((pid = waitpid(WAIT_ANY, &status, WNOHANG)) > 0){
		update_status(&jobsList);
		 struct list_elem * jobLink = list_begin(&jobsList);
		 for (; jobLink != list_end(&jobsList); jobLink = list_next(jobLink)){
		 	struct esh_pipeline * job = list_entry(jobLink, struct esh_pipeline, elem);
		 	struct list_elem * commandLink = list_begin(&job->commands);
		 	for (; commandLink != list_end(&job->commands); commandLink = list_next(commandLink)){
		 		struct esh_command * cmd = list_entry(commandLink, struct esh_command, elem);
		 		if (cmd->pid == pid){
					if(status == 9 || status == SIGINT){
						list_remove(jobLink);
						esh_pipeline_free(job);
					}
					else if(status == SIGTSTP){
						job->status = STOPPED;
					}
		 		}
		 	}
		 }
	}
	give_terminal_to(shell_pgrp, shell_state);
}

static int jobNum;
struct list jobsList;
struct termios * shell_state;

int
main(int ac, char *av[])
{   
    setpgid(0, 0);
    int shell_terminal = STDIN_FILENO;
    int shell_is_interactive = isatty(shell_terminal);
    jobNum = 0;
    int opt;
    list_init(&esh_plugin_list);
    shell_pgrp = getpid();
    shell_state = esh_sys_tty_init();
    
    signal(SIGCHLD, handler);


    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "hp:")) > 0) {
        switch (opt) {
        case 'h':
            usage(av[0]);
            break;

        case 'p':
            esh_plugin_load_from_directory(optarg);
            break;
        }
    }

    esh_plugin_initialize(&shell);
    list_init(&jobsList);
    /* Read/eval loop. */
    for (;;) {

    	give_terminal_to(shell_pgrp, shell_state);
	    /*Update the status of all existing jobs and notify user of completed jobs*/
	    do_job_notification(0);
	
	    /*Reset the job count if there are no pending jobs*/
	    if(list_begin(&jobsList) == list_end(&jobsList)){
            jobNum = 0;
        }

		// Give the shell control of the terminal and restore its sane state
		// give_terminal_to(shell_pgrp, shell_state);	


		esh_signal_unblock(SIGCHLD);
		esh_signal_unblock(SIGTTOU);

        /* Do not output a prompt unless shell's stdin is a terminal */
        char * prompt = isatty(0) ? shell.build_prompt() : NULL;
        char * cmdline = shell.readline(prompt);
        free (prompt);

		esh_signal_block(SIGCHLD);
		esh_signal_block(SIGTTOU);


        if (cmdline == NULL)  /* User typed EOF */
            break;

        struct esh_command_line * cline = shell.parse_command_line(cmdline);
        free (cmdline);
        if (cline == NULL)                  /* Error in command line */
            continue;

        if (list_empty(&cline->pipes)) {    /* User hit enter */
            esh_command_line_free(cline);
            continue;
        }
	
		//printf("\n what is going on here\n");

        struct list_elem * pipe = list_begin(&cline->pipes);

	    /* Is the command a built-in function? */

        //if it is not a build in command
		if(!is_builtin_command(pipe)){
			esh_command_line_print(cline);

			esh_signal_block(SIGCHLD);

			list_remove(pipe);
			list_push_back(&jobsList, pipe);

			esh_signal_unblock(SIGCHLD);
			struct esh_pipeline * pipeline = list_entry(pipe, struct esh_pipeline, elem);
			jobNum++;
			pipeline->jid = jobNum;

			if(pipeline->bg_job){
				pipeline->status = BACKGROUND;
			}
			else{
				pipeline->status = FOREGROUND;
			}
			
			
			launch_job(pipeline, pipeline->bg_job, shell_is_interactive, shell_pgrp, shell_state);
	            
		
		}
	
        //if it is a build-in command
	    else{
		    struct esh_pipeline * pipeline = list_entry(pipe, struct esh_pipeline, elem);
		    struct list_elem * commandLink = list_begin(&pipeline->commands);
		    struct esh_command * command = list_entry(commandLink, struct esh_command, elem);

            char * cmd =  command->argv[0];

            //if the command is jobs
            if (strcmp(cmd, "jobs") == 0){
			    struct list_elem * jobLink = list_begin(&jobsList);
			    for (; jobLink != list_end(&jobsList); jobLink = list_next(jobLink)){
				    struct esh_pipeline * job = list_entry(jobLink, struct esh_pipeline, elem);
				    printf("[%d] ", job->jid);
				    switch(job->status){
					   case FOREGROUND: printf("Running     ");break;
					   case BACKGROUND: printf("Running     ");break;
					   case STOPPED: printf("Stopped        ");break;
					   case NEEDSTERMINAL: printf("Stopped  ");break;

					}
					printf("(");
					print_job_name(job);
					printf(")");
					printf("\n");
				}	
           	}	
            
            // if the command is fg, move the target to foreground
	    	else if (strcmp(cmd, "fg") == 0){
                foreground(atoi(command->argv[1]), &jobsList, shell_pgrp, shell_state);
            }
			// if the command is bg, move the target to background
            else if (strcmp(cmd, "bg") == 0){
				struct list_elem * jobLink = list_begin(&jobsList);
				int found = 0;
				for (; jobLink != list_end(&jobsList); jobLink = list_next(jobLink)){
					struct esh_pipeline * job = list_entry(jobLink, struct esh_pipeline, elem);
					if(job->jid == atoi(command->argv[1])){
						found = 1;
						continue_job(job, true, shell_pgrp, shell_state);
					}
				}
				if (!found){
					printf("No job with matching ID \n");
				}
		    }


    		//if the command is kill, kill the target
    	    else if (strcmp(cmd, "kill") == 0){
                struct list_elem * jobLink = list_begin(&jobsList);
                int found = 0;
                for (; jobLink != list_end(&jobsList); jobLink = list_next(jobLink)){
                    struct esh_pipeline * job = list_entry(jobLink, struct esh_pipeline, elem);
                    if(job->jid == atoi(command->argv[1])){
                       found = 1;

                       //kill
                       kill (- job->pgrp, SIGKILL);

                       //remove from the list
                       list_remove(jobLink);
                    }
                }
                if (!found){
                    printf("No job with matching ID \n");
                }
          			
    	   }

            //if the command is stop, stop the target
    	    else if (strcmp(cmd, "stop") == 0){

                struct list_elem * jobLink = list_begin(&jobsList);
                int found = 0;
                for (; jobLink != list_end(&jobsList); jobLink = list_next(jobLink)){
                    struct esh_pipeline * job = list_entry(jobLink, struct esh_pipeline, elem);
                    if(job->jid == atoi(command->argv[1])){
						esh_sys_tty_save(&job->saved_tty_state);
                       	found = 1;
                       	kill(-job->pgrp, SIGTSTP);
						job->status = STOPPED;
                    }
                }
                if (!found){
                    printf("No job with matching ID\n");
                }
    	    }

            //if the command is quit_shell, quit the program
    	    else if (strcmp(cmd, "quit_shell") == 0){
    	    	return 0;
    	    }

	    }	
        esh_command_line_free(cline);
    }
    return 0;
}

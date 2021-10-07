/* 
 * tsh - A tiny shell program with job control
 * 
 * Brittany Bergeron
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

/* Misc manifest constants */
#define MAXLINE    1024		/* max line size */
#define MAXARGS     128		/* max args on a command line */
#define MAXJOBS      16		/* max jobs at any point in time */
#define MAXJID    1<<16		/* max job ID */

/* Job states */
#define UNDEF 0		/* undefined */
#define FG 1		/* running in foreground */
#define BG 2		/* running in background */
#define ST 3		/* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */

extern char **environ;		/* defined in libc */
char prompt[] = "tsh> ";	/* command line prompt */
int verbose = 0;			/* if true, print additional output */
int nextjid = 1;			/* next job ID to allocate */
char sbuf[MAXLINE];			/* for composing sprintf messages */

/* The job struct */
struct job_t { 
    pid_t pid;				/* job PID */
    int jid;				/* job ID [1, 2, ...] */
    int state;				/* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];	/* command line */
};

/* The job list */
struct job_t jobs[MAXJOBS]; 


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void do_redirect(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);


/* main - The shell's main routine */
int main(int argc, char **argv) {
	char c;
	char cmdline[MAXLINE];
	 int emit_prompt = 1; /* emit prompt (default) */

	/* Redirect stderr to stdout (so that driver will get all output on the pipe connected to stdout) */
	/*copy file descriptor*/
	dup2(1, 2);

	/* Parse the command line */
	while ((c = getopt(argc, argv, "hvp")) != EOF) {
		switch (c) {
			case 'h':				/* print help message */
			usage();
			break;
			case 'v':				/* emit additional diagnostic info */
				verbose = 1;
				break;
			case'p':				/* don't print a prompt */
				emit_prompt = 0;	/* handy for automatic testing */
				break;
			default:
				usage();
		}
	} 

	/* Install the signal handlers */

	/* These are the ones you will need to implement */
	Signal(SIGINT,  sigint_handler);   /* ctrl-c */
	Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
	Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

	/* Ignoring these signals simplifies reading from stdin/stdout */
	Signal(SIGTTIN, SIG_IGN);            /* ignore SIGTTIN */
	Signal(SIGTTOU, SIG_IGN);          /* ignore SIGTTOU */

	/* This one provides a clean way to kill the shell */
	Signal(SIGQUIT, sigquit_handler); 

	/* Initialize the job list */
	initjobs(jobs);

	/* Execute the shell's read/eval loop */
	while (1) {
		/* Read command line */
		if (emit_prompt) {
			printf("%s", prompt);
			fflush(stdout);
		}
		if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
			app_error("fgets error");
		
		if (feof(stdin)) {                      /* End of file (ctrl-d) */
			fflush(stdout);
			exit(0);
		}

		/* Evaluate the command line  and flush stdout buffer*/
		eval(cmdline);
		fflush(stdout);
		fflush(stdout);
	} 

	exit(0); /* control never reaches here */
}//end main

/* 
* eval - Evaluate the command line that the user has just typed in
* 
 * If the user has requested a built-in command (quit, jobs, bg or fg) then execute it immediately. 
 *Otherwise, fork a child process and run the job in the context of the child. 
 *If the job is running in the foreground, wait for it to terminate and then return.  
 *Note: each child process must have a unique process group ID so that our
 *       background children don't receive SIGINT (SIGTSTP) from the kernel
 *       when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) {
	/*csapp: page 735*/
	/*ch.8 sides for signal handling)*/

	//character pointer for arg list 
	char *argv[MAXARGS];
	//determines if background or foreground
	int bg;
	//pid of current(parent, child, etc)
	pid_t pid;

	//mask for sigproc
	sigset_t mask;
	//create the empty set
	sigemptyset(&mask);
	//add our signals
	sigaddset(&mask, SIGCHLD);
	sigaddset(&mask, SIGSTOP);
	sigaddset(&mask, SIGINT);

	//assign if bg or fg based on input
	bg = parseline(cmdline, argv);
	
	//undefined, return
	if(argv[0] == NULL){
		return;
	}
	//check if built in; execute if so, else continue
	if(!builtin_cmd(argv)){

		/*handling some pid and fork stuff, error control*/
		//block to avoid race condition
		sigprocmask(SIG_BLOCK, &mask, NULL);		/*cs:app, 753-754, slide 99/108 ch 8 for signal blocking*/
		//fork process, set pid (region not interrupted by block)
		pid = fork();

		if(pid < 0){
			//print error
			unix_error("Fork error");
			//return pid;
			return;
		}

		//child will return 0 and execute this if statement
		if(pid == 0){
			do_redirect(argv);
			//keep child out of forground process group
			setpgid(0,0);
			//unblock in child fork
			sigprocmask(SIG_UNBLOCK, &mask, NULL);

			//returns an error message and quits process if not applicable cmd(execve returned for error)
			if(execve(argv[0], argv, environ) < 0){
				printf("%s: Command not found.\n", argv[0]);
				exit(0);
			}
		}
		//pid is negative: error
		

		/*parent job*/

		/*determine fg/bg jobs*/
		//foreground jobs
		if(!bg){
			//add to jobs list
			addjob(jobs, pid, FG, cmdline);
		}

		//background jobs
		else{
			//still need to add onto jobs list
			addjob(jobs, pid, BG, cmdline);
			//pid2jid is included, uses formatting to match tshref
			printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
		}
		//unblock after added job, (need to unblock if pid =/= 0)
		sigprocmask(SIG_UNBLOCK, &mask, NULL);

		//waitfg so job finishes before next
		if(!bg){waitfg(pid);}
	}
	return;
	
}//end eval


/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) {
	static char array[MAXLINE]; 	/* holds local copy of command line */
	char *buf = array;         				  /* ptr that traverses command line */
	char *delim;               					  /* points to first space delimiter */
	int argc;                  						  /* number of args */
	int bg;                    						   /* background job? */

	strcpy(buf, cmdline);
	buf[strlen(buf)-1] = ' ';  				/* replace trailing '\n' with space */
	while (*buf && (*buf == ' ')) 	  /* ignore leading spaces */
		buf++;

	/* Build the argv list */
	argc = 0;
	if (*buf == '\'') {
		buf++;
		delim = strchr(buf, '\'');
	}
	else {
		delim = strchr(buf, ' ');
	}

	while (delim) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* ignore spaces */
			buf++;

		if (*buf == '\'') {
			buf++;
			delim = strchr(buf, '\'');
		}
		else {
			delim = strchr(buf, ' ');
		}
	}
	argv[argc] = NULL;

	//return 1 for built-in
	if (argc == 0)  /* ignore blank line */
	return 1;

	/* should the job run in the background? */
	if ((bg = (*argv[argc-1] == '&')) != 0) {
		argv[--argc] = NULL;
	}
	return bg;
}

/* 
* builtin_cmd - If the user has typed a built-in command then execute
*    it immediately.  
*/
int builtin_cmd(char **argv) {
	//(cs:app page 735)
	//4 of these;quit, jobs, bg or fg
	
	//quit the tsh by exiting
	if(!strcmp(argv[0], "quit")){
		exit(0);		//exit does not return
	}
	//display the current jobs list by calling jobs (already implemented)
	if(!strcmp(argv[0], "jobs")){
		listjobs(jobs);
		//return 1 for built-in
		return 1;
	}
	//bg or fg is handled with do_bgfg
	if( (!strcmp(argv[0],"bg") || (!strcmp(argv[0], "fg"))) ){
		do_bgfg(argv);
		//return 1 for built-in
		return 1;
	}	

	return 0;     /* not a builtin command */
}


/* 
* do_redirect - scans argv for any use of < or > which indicate input or output redirection
*
*/
void do_redirect(char **argv){
	int i;

	for(i=0; argv[i]; i++){
		if (!strcmp(argv[i],"<")) {
			/* add code for input redirection below */
			int fdin = open(argv[i+1], O_RDONLY, 0);
				//error handling
			if(fdin < 0){
				perror("open");
				return;
			}
			//STDIN_FILENO is in header <unistd.h> -- cs:app 863
			dup2(fdin, STDIN_FILENO);
			//error handling
			if(close(fdin) < 0){
				perror("close");
				return;
			}
			
			/* the line below cuts argv short -- this removes the < and whatever follows from argv */
			argv[i]=NULL;
		}
		else if (!strcmp(argv[i],">")) {
			/* do stuff for output redirection here */
			int fdout = open(argv[i+1], O_WRONLY|O_CREAT,  S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
			//error handling
			if(fdout < 0){
				perror("open");
				return;
			}
			dup2(fdout, STDOUT_FILENO);
			//error handling
			if(close(fdout) < 0){
				perror("close");
				return;
			}
			
			/* the line below cuts argv short -- this removes the > and whatever follows from argv */
			argv[i]=NULL;
		}
	}
}

/* 
* do_bgfg - Execute the builtin bg and fg commands
*/
void do_bgfg(char **argv){
	//jobs can be recognized with PID or JID
	pid_t pid;
	int jid;
	//hold current job to look at
	struct job_t *this_job;

	/*determine if pid or jid; jid is +(%#), pid is  +(#)*/
	
	//handle no input
	if(argv[1] == NULL){
		printf("%s command requires PID or %%jobid argument\n", argv[0]);
		return;
	}

	//jid
	else if(argv[1][0] == '%'){	
		//jid; get rid of % by referencing next index; atoi(ascii to integer)
		jid = atoi(&argv[1][1]);
		//now convert to the job
		this_job = getjobjid(jobs,jid);
		//error handling
		if(this_job == NULL){
			printf("%s: No such job\n",argv[1]);
			return;
		}

		//and store the pid
		pid = this_job->pid;
	}
	//pid
	else if(isdigit(*argv[1])){
		//pid; store in var
		pid = atoi(argv[1]); 
		//save job with given function
		this_job = getjobpid(jobs, pid);
		if(this_job == NULL){
			printf("(%d): No such process\n",pid);
			return;
		}
	}
	/*handle bad input*/
	else{
		printf("%s: argument must be a PID or %%jobid\n",argv[0]);
		return;
	}

	/*see if "fg" or "bg' was used (built in)*/
	
	//if switching to background, 
	if(!strcmp("bg", argv[0])){
		//change defined state
		this_job->state = BG;
		//print format information
		printf("[%d] (%d) %s", pid2jid(pid), pid, (this_job->cmdline));
		//kill group with SIGCONT
		kill(-pid, SIGCONT);
	}
	
	//if switching to foreground
	else if(!strcmp("fg", argv[0])){
		//switch state
		this_job->state = FG;
		//kill before waitfg
		kill(-pid, SIGCONT);
		//and run wait fg since its now in fg
		waitfg(pid);
	}
	
	return;
}//end do_bgfg

/* 
* waitfg - Block until process pid is no longer the foreground process
*/
void waitfg(pid_t pid){
	/*don't call waitpid; use busyloop/sleep*/
	
	//pointer to current job
	struct job_t* currentjob;
	
	//check jobs list getjobpid()
	currentjob = getjobpid(jobs, pid);

	if(!currentjob){
		return;
	}

	//fgpid returns pid of current fg, or 0 if none
	//so, if the pid we are waiting for is in the fg, the loop will run; else it will not equal 0
	while((currentjob->state) == FG){
		//do nothing, sleeps until false
		sleep(1);
	}
	//sleep time over, continue
	return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) {
	/*cs:app page 727*/
	/*cs:app page 745*/
	/*slides, ch.8 signals*/
	
	int status;
	pid_t pid;
	//waitpid reaps; 

	while((pid = waitpid(-1, &status, WUNTRACED|WNOHANG))>0){
	
		if(WIFEXITED(status)){
			//kill job
			deletejob(jobs, pid);
		}
		else if(WIFSIGNALED(status)){
			//interrupted, so delete 
			//feedback on action according to tshref	/*CSAPP 725: WTERMSIG returns number of signal that caused terminate
			printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
			deletejob(jobs, pid);
		}
		else if(WIFSTOPPED(status)){
			//get the job with gjp
			struct job_t * thisjob = getjobpid(jobs, pid);			
			//change state
			thisjob->state = ST;
			
			//print message	
			printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
		}
	}
	return;
}

/* 
* sigint_handler - The kernel sends a SIGINT to the shell whenver the
*    user types ctrl-c at the keyboard.  Catch it and send it along
*    to the foreground job.  
*/
void sigint_handler(int sig) {
	
	/*need to distinguish that the fg job is what should be handled*/
	
	pid_t pid;
	//current fg pid can be obtained with fgpid() built in
	pid = fgpid(jobs);
	//if no fg job, no effect
	if(getjobpid(jobs, pid) == NULL){
		return;
	}

	//kill passes along to chld handler (-pid for group)
	kill(-pid, sig);
	return;
}

/*
* sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
*     the user types ctrl-z at the keyboard. Catch it and suspend the
*     foreground job by sending it a SIGTSTP.  
*/
void sigtstp_handler(int sig){
	/*need to distinguish that the fg job is what should be handled*/
	
	pid_t pid;
	//current fg pid can be obtained with fgpid() built in
	pid = fgpid(jobs);
	//if no fg job, no effect
	if(getjobpid(jobs, pid) == NULL){
		return;
	}
	
	//kill passes along to chld handler (-pid for group)
	kill(-pid, sig);
	return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
	job->pid = 0;
	job->jid = 0;
	job->state = UNDEF;
	job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
	int i, max=0;

	for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
		max = jobs[i].jid;
	return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
	int i;
	
	if (pid < 1)
	return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == 0) {
			jobs[i].pid = pid;
			jobs[i].state = state;
			jobs[i].jid = nextjid++;
			if (nextjid > MAXJOBS)
				nextjid = 1;
			strcpy(jobs[i].cmdline, cmdline);
			if(verbose){
				printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
			}
			return 1;
		}
	}
	printf("Tried to create too many jobs\n");
	return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
	int i;

	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == pid) {
			clearjob(&jobs[i]);
			nextjid = maxjid(jobs)+1;
			return 1;
		}
	}
	return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].state == FG)
			return jobs[i].pid;
	return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
	int i;

	if (pid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid)
			return &jobs[i];
	return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) {
	int i;

	if (jid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid == jid)
			return &jobs[i];
	return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
	int i;

	if (pid < 1)
		return 0;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid) {
			return jobs[i].jid;
		}
	return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
	int i;
	
	for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
		printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
		switch (jobs[i].state) {
		case BG: 
			printf("Running ");
			break;
		case FG: 
			printf("Foreground ");
			break;
		case ST: 
			printf("Stopped ");
			break;
		default:
			printf("listjobs: Internal error: job[%d].state=%d ", 
			i, jobs[i].state);
		}
		printf("%s", jobs[i].cmdline);
	}

}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
	printf("Usage: shell [-hvp]\n");
	printf("   -h   print this message\n");
	printf("   -v   print additional diagnostic information\n");
	printf("   -p   do not emit a command prompt\n");
	exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
	fprintf(stdout, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
	fprintf(stdout, "%s\n", msg);
	exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
	struct sigaction action, old_action;

	action.sa_handler = handler;  
	sigemptyset(&action.sa_mask); /* block sigs of type being handled */
	action.sa_flags = SA_RESTART; /* restart syscalls if possible */

	if (sigaction(signum, &action, &old_action) < 0)
		unix_error("Signal error");
	return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
	printf("Terminating after receipt of SIGQUIT signal\n");
	exit(1);
}
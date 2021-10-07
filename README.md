# Personal Weather Station
***
## Task
Build a functioning shell using the given parsing code that can control multiple jobs (processes), background and forground, using PID and signal handling.
***
## Criteria
* Use C language
* Implement signal handlers (sigchld, sigstp, sigint)
* Create new jobs by fork()
* Process built-in commands immediately
* Executables added to job list in context of child process
***
## Summary
tsh (TinyShell) is a command-line interpreter that parses input and executes relevant functions. They are either built-in commands that update the state of the shell or executable paths that fork into a child process. The jobs can be either background or foreground. One job can run as forground at a time.
***
## Functionality
Built in commands:
* jobs - lists the jobs present, even if they are stopped at the moment
* bg - change job to run in the background
* fg - change a background job into a foreground job
* kill - terminates this job
supports:
* pipes - |
* redirection - < >
* stop - ctrl+c
* switcxh to background - &
***
## Design
Tsh is designed for simple functionality. The commands work as they would in a Unix environment, and they should feel as such.
***
## Run Locally
using gcc compiler(linux):
* gcc tinyShell.c -o tsh
* ./tsh

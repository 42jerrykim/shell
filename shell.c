#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_CMD_ARG 	10
#define MAX_CMD_LIST 	10
#define MAX_CMD_GRP	10

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

const char *prompt = "myshell ";

char* cmdgrp[MAX_CMD_GRP];
char* cmdlist[MAX_CMD_LIST];
char* cmdargs[MAX_CMD_ARG];
char cmdline[BUFSIZ];

void fatal(char *str);
void parse_redirect(char* cmd);
void execute_cmd(char *cmdlist);
void execute_cmdline(char* cmdline);
void execute_cmdgrp(char* cmdgrp);
void zombie_handler(int signo);
int makeargv(char *s, const char *delimiters, char** argvp, int MAX_LIST);


struct sigaction act;
static int status;
static int IS_BACKGROUND=0;

typedef struct { // 커맨드 구조체
    char* name;
    char* desc;
    int ( *func )( int argc, char* argv[] ); // 함수포인터. 사용할 함수들의 매개변수를 맞춰줌
} COMMAND;

int cmd_cd( int argc, char* argv[] ){ //cd : change directory
    if( argc == 1 )
        chdir( getenv( "HOME" ) );
    else if( argc == 2 ){
        if( chdir( argv[1] ) )
            printf( "No directory\n" );
    }
    else
        printf( "USAGE: cd [dir]\n" );
    
    return TRUE;
}

int cmd_exit( int argc, char* argv[] ){
    printf("!!! Exit !!!\n");
    exit(0);
    
    return TRUE;
}

static COMMAND builtin_cmds[] =
{
    { "cd", "change directory", cmd_cd },
    { "exit", "exit this shell", cmd_exit },
    { "quit", "quit this shell", cmd_exit },
    
};

int main(int argc, char**argv)
{
	int i;
	sigset_t set;
	
	sigfillset(&set);
	sigdelset(&set,SIGCHLD);
	sigprocmask(SIG_SETMASK,&set,NULL);
    
	act.sa_flags = SA_RESTART;
	sigemptyset(&act.sa_mask);
	act.sa_handler = zombie_handler;
	sigaction(SIGCHLD, &act, 0);
	
    while (1) {
        fputs(prompt, stdout);
        printf( "[%s] $ ", get_current_dir_name() );
        fgets(cmdline, BUFSIZ, stdin);
        cmdline[ strlen(cmdline) -1] ='\0';
		execute_cmdline(cmdline);
	}
	return 0;
}

void zombie_handler(int signo)
{
    pid_t pid ;
    int stat ;
    
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
        printf("child %d terminated normaly\n", pid) ;
}

void fatal(char *str)
{
	perror(str);
	exit(1);
}

int makeargv(char *s, const char *delimiters, char** argvp, int MAX_LIST)
{
	int i = 0;
	int numtokens = 0;
	char *snew = NULL;
    
	if( (s==NULL) || (delimiters==NULL) )
	{
		return -1;
	}
    
	snew = s+strspn(s, delimiters);
	
	argvp[numtokens]=strtok(snew, delimiters);
	
	if( argvp[numtokens] !=NULL)
		for(numtokens=1; (argvp[numtokens]=strtok(NULL, delimiters)) != NULL; numtokens++)
		{
			if(numtokens == (MAX_LIST-1)) return -1;
		}
    
	if( numtokens > MAX_LIST) return -1;
    
	return numtokens;
}

void parse_redirect(char* cmd)
{
	char *arg;
	int cmdlen = strlen(cmd);
	int fd, i;
	
	for(i = cmdlen-1;i >= 0;i--)
	{
		switch(cmd[i])
		{
			case '<':
				arg = strtok(&cmd[i+1], " \t");
				if( (fd = open(arg, O_RDONLY | O_CREAT, 0644)) < 0)
					fatal("file open error");
				dup2(fd, STDIN_FILENO);
				close(fd);
				cmd[i] = '\0';
				break;
			case '>':
				arg = strtok(&cmd[i+1], " \t");
                if( (fd = open(arg, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
					fatal("file open error");
                dup2(fd, STDOUT_FILENO);
                close(fd);
                cmd[i] = '\0';
				break;
			default:break;
		}
	}
    
}

int parse_background(char *cmd)
{
	int i;
    
    for(i=0; i < strlen(cmd); i++)
        if(cmd[i] == '&')
        {
            cmd[i] = ' ';
            return 1;
        }
			
    
	return 0;
}

void execute_cmd(char *cmdlist)
{
    parse_redirect(cmdlist);
    
    if(makeargv(cmdlist, " \t", cmdargs, MAX_CMD_ARG) <= 0)
		fatal("makeargv_cmdargs error");
	
    execvp(cmdargs[0], cmdargs);
    fatal("exec error");
}

void execute_cmdgrp(char *cmdgrp)
{
	int i=0;
	int count = 0;
	int pfd[2];
    sigset_t set;
    
	setpgid(0,0);
 	if(!IS_BACKGROUND)
        tcsetpgrp(STDIN_FILENO, getpid());
    
    sigfillset(&set);
    sigprocmask(SIG_UNBLOCK,&set,NULL);
    
    if((count = makeargv(cmdgrp, "|", cmdlist, MAX_CMD_LIST)) <= 0)
        fatal("makeargv_cmdgrp error");
    
	for(i=0; i<count-1; i++)
    {
		pipe(pfd);
		switch(fork())
		{
			case -1: fatal("fork error");
            case  0: close(pfd[0]);
                dup2(pfd[1], STDOUT_FILENO);
                execute_cmd(cmdlist[i]);
            default: close(pfd[1]);
                dup2(pfd[0], STDIN_FILENO);
		}
	}
	execute_cmd(cmdlist[i]);
    
}

void execute_cmdline(char* cmdline)
{
    int count = 0;
    int i=0, j=0, pid;
    char* cmdvector[MAX_CMD_ARG];
    char cmdgrptemp[BUFSIZ];
    int numtokens = 0;
    
    count = makeargv(cmdline, ";", cmdgrp, MAX_CMD_GRP);
    
    for(i=0; i<count; ++i)
    {
        memcpy(cmdgrptemp, cmdgrp[i], strlen(cmdgrp[i]) + 1);
        numtokens = makeargv(cmdgrp[i], " \t", cmdvector, MAX_CMD_GRP);
        
        for( j = 0; j < sizeof( builtin_cmds ) / sizeof( COMMAND ); j++ ){            if( strcmp( builtin_cmds[j].name, cmdvector[0] ) == 0 ){
                builtin_cmds[j].func( numtokens , cmdvector );
                return;
            }
        }
        
		IS_BACKGROUND = parse_background(cmdgrptemp);
        
        switch(pid=fork())
        {
            case -1:
                fatal("fork error");
            case  0:
                execute_cmdgrp(cmdgrptemp);
            default:
                if(IS_BACKGROUND) break;
                waitpid(pid, NULL, 0);
                tcsetpgrp(STDIN_FILENO, getpgid(0));
                fflush(stdout);
        }
    }
    
}



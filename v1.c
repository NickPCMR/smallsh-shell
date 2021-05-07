#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

//global variables
int exitStatus = 0;
int foreground = 0;
int pid;
struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};

void handle_SIGTSTP(int signo)
//signal handler for control-z (SIGTSTP)
//receiving the SIGTSTP signal will cause the program to toggle foreground only mod
//allowing processses to only be run in the foreground
{

    // toggle message and foreground variable everytime signal is sent
    char *message;
    if (foreground)
    {
        foreground = 0;
        message = "Exiting foreground-only mode\n";
    }
    else
    {
        message = "Entering foreground-only mode (& is now ignored)\n";
        foreground = 1;
    }

    // write message
    write(STDOUT_FILENO, message, strlen(message));
    fflush(stdout);
    raise(SIGINT);
}

void printStatus()
{
    // this function prints the exit status of the last run foreground process
    // which is stored in the exitStatus variable
    printf("exit status %d\n", exitStatus);
    fflush(stdout);
    fflush(stdout);
}

void execCommand(char *cmd[], char *infile, char *outfile, int background)
//this function executes commands using execvp
//cmd[] contains the list of commands and arguments to pass to execvp
//infile is a pointer to the filename for input redirection
//outfile is a pointer to te filename for ouput redireciton
//background is a boolean that will determine if command is executed in foreground or background
{

    int childStatus;

    // Fork a new process
    pid_t spawnPid = fork();

    switch (spawnPid)
    {

    case -1:
        // print error if fork was unsuccessful
        perror("fork()\n");
        exit(1);
        break;
    case 0:
        // In the child process

        // let ^c kill the foreground process by resetting the handler to default action
        SIGINT_action.sa_handler = SIG_DFL;
        sigaction(SIGINT, &SIGINT_action, NULL);

        // if outfile is not NULL, redirect output to outfile
        if (outfile)
        {
            // open file descripter
            int outFD = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);

            if (outFD == -1)
            //print error if unsuccessful
            {
                perror("open()");
                exit(1);
            }

            //redirect standard output to the output file using dup2()
            int result = dup2(outFD, 1);

            if (result == -1)
            {
                //print error if unsucessful
                perror("dup2");
                exit(2);
            }
            //close the file descriptor on execution
            fcntl(outFD, F_SETFD, FD_CLOEXEC);
        }

        // if infile is not NULL, get input from infile
        if (infile)
        {
            // open file descriptor
            int inFD = open(infile, O_RDONLY);

            if (inFD == -1)
            // handle error
            {
                perror("open()");
                exit(1);
            }

            // redirect standard input to infile using dup2()
            int inResult = dup2(inFD, 0);

            if (inResult == -1)
            //handle error
            {
                perror("dup2");
                exit(2);
            }
            //close fd on execute
            fcntl(inFD, F_SETFD, FD_CLOEXEC);
        }

        // pass arguments to execvp and try to execute command
        if (execvp(cmd[0], cmd) == -1)
        {
            // change exitStatus to unsuccessful if execvp fails
            exitStatus = 1;
        }
        else
        {
            //change exitStatus to successful if execvp succeeds
            exitStatus = 0;
        }

        // exec only returns if there is an error
        perror("execvp");
        fflush(stdout);

        exit(2);
        break;
    default:
        // In the parent process

        // if background was not NULL and program is not in foreground only mode,
        // continue execution and do not wait for child to terminate
        if (background && !foreground)
        {
            //spawnPid = waitpid(spawnPid, &childStatus, WNOHANG);
            printf("background pid is %d\n", spawnPid);

            fflush(stdout);
        }

        else
        //wait for child process to finish
        {
            spawnPid = waitpid(spawnPid, &childStatus, 0);
        }

        // check for background processes that were termianted by a signal
        if (WIFSIGNALED(childStatus) == 1 && WTERMSIG(childStatus) != 0)
        {
            printf("terminated by signal %d", WTERMSIG(childStatus));
        }

        //check for terminated background processes
        pid = waitpid(-1, &childStatus, WNOHANG);
        while (pid > 0)
        {

            if (WIFEXITED(childStatus) != 0 && pid > 0)
            {

                printf("child %d terminated, signal %d\n", pid, childStatus);
                fflush(stdout);
            }
            pid = waitpid(-1, &childStatus, WNOHANG);
        }

        break;
    }
}

void changeDir(char *token)
// function to handle cd command
// chdir to provided token path, or to home directory if token is NULL
{

    if (token == NULL)
    {
        chdir(getenv("HOME"));
    }
    else
    {
        chdir(token);
    }
}

void exitAll()
//fnction for exit command
//sends kill signal to all group processes
{

    gid_t group = getgid();
    kill(group, SIGKILL);
    exit(0);
}

void cmdList(char *key[], char *infile, char *outfile, int background)
//function checks to see if the first argument listed by the user is one of the built in commands
//cd, exit, status or a comment (#)
{

    if (strcmp(key[0], "exit") == 0)
    {
        exitAll();
    }

    if (strcmp(key[0], "cd") == 0)
    {
        changeDir(key[1]);
        return;
    }

    if (strcmp(key[0], "status") == 0)
    {
        printStatus();
        return;
    }
    if (strncmp(key[0], "#", 1) == 0)
    {
        return;
    }

    // if first argument doesn't match a built in command,
    // pass it to execCommand and try to run it with execvp()
    execCommand(key, infile, outfile, background);
    return;
}

char *prompt()
//prompt the user for input and return it
{

    char *input = malloc(sizeof(char) * 2048);
    printf(":");
    fflush(stdout);
    fgets(input, 2048, stdin);
    return input;
}

int charCheck(char *str)
// this function checks for special characters in the input
// such as > < &, and returns a specific value if they are found
{

    if (strcmp(str, ">") == 0)
    {
        return 1;
    }
    if (strcmp(str, "<") == 0)
    {
        return 2;
    }
    if (strcmp(str, "&") == 0)
    {
        return 3;
    }

    //return 0 if nothing found
    return 0;
}

void processInput(char *str)
// this function processes the user input string passed in the str variable
// the string is broken into tokens separated by spaces.
// then they are checked for special characters
{
    char *input[512];                 // input string
    int size = 0;                     // keep track of the size of the input string
    char *token = strtok(str, " \n"); // get the first token of te string
    char *infile = 0;                 // pointer for input file if found
    char *outfile = 0;                // pointer for output file if found
    int background = 0;               // boolean to track if command should be run in bg or fg
    int flag;

    // handle empty string input
    if (token == NULL)
    {
        return;
    }

    // loop through the input string and grab a new token each time
    while (token != NULL)
    {
        // handle $$ by swapping it with process id
        if (strstr(token, "$$"))
        {
            //assuming $$ was at the end of the token, remove them
            token[strlen(token) - 2] = '\0';
            //now at the pid onto the token
            sprintf(token, "%s%d", token, getpid());
            fflush(stdout);
        }

        // check for special character
        flag = charCheck(token);

        if (flag == 1)
        { // if output character detected, set outfile pointer to next token
            outfile = strtok(NULL, " \n");
            //get next token
            token = strtok(NULL, " \n");
            continue;
        }
        if (flag == 2)
        { // if input character detected, set infile pointer to next token
            infile = strtok(NULL, " \n");
            //get next token
            token = strtok(NULL, " \n");
            continue;
        }
        if (flag == 3)
        { // if background detected, set background boolean flag
            background = 1;
            token = strtok(NULL, " \n");
            continue;
        }

        // if no special characters were found, read the token into the input[] array
        input[size] = token;
        //get next token and iterate loop
        token = strtok(NULL, " \n");
        size++;
    }

    //make 2nd array for only needed size
    char *args[size];

    for (int i = 0; i < size; i++)
    {
        args[i] = input[i];
    }

    // last argument set to NULL for compatibilty with execvp()
    args[size] = NULL;

    //pass the arguments and pointers to  cmdList to check for built in commands
    cmdList(args, infile, outfile, background);

    //free mem
    free(str);
}

int main(int argc, char *argv[])
{

    // create signal handlers
    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0}, SIGCHLD_action = {0};

    SIGINT_action.sa_handler = SIG_IGN;
    // Block all catchable signals while handle_SIGINT is running
    sigfillset(&SIGINT_action.sa_mask);
    // No flags set
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    // Block all catchable signals while handling SIGTSTP is running
    sigfillset(&SIGTSTP_action.sa_mask);
    // No flags set
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    while (1)
    //input loop, can only be exited with "exit" command
    {
        processInput(prompt());
    }
    return EXIT_SUCCESS;
}

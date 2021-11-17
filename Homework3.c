/*Name: Richard Frank
 * Date: 11/1/2021
 * 
 * Description: This program contains all functionality that would be expected in a lightweight command shell.
 *  The program has built in functionality to handle the commands "cd", "exit", and "status" but supports other 
 *  commands by leveraging the execvp function and forking those into a child process. The program also supports
 *  foreground and background processes using the "&" operator, as well as input and output redirection.
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_LENGTH 2049
#define MAX_ARGS 512

/*
 *Globals are set to track background/foreground toggling and necessary status's.
 */
int backgroundMode = 0;
struct sigaction sigint = { 0 };
struct sigaction sigtstp = { 0 };
int statusFlag = 0;

/*
 *The processArray and processIndex "exit" global are used to track running processes and allow for a graceful exit
 *   when the user enters 
 */
pid_t* processArray[MAX_ARGS] = { NULL };
int processIndex = 0;

/*
 *This function is a custom handler for the SIGINT signal to toggle the background and foreground function.
 *   This function is based off of examples provided in the signal modules.
 */
void catchSIGINT(int signalNumber) {
    const char* off_message = "terminated by signal 2\n";
    write(1, off_message, strlen(off_message));
    fflush(stdout);
    return;
}

/*
 *This function is a custom handler for the SIGTSTP signal to toggle the background and foreground function.
 *  Ths function is based off of examples provided in the signal modules.
 */

void catchSIGTSTP(int signalNumber) {
    if (backgroundMode == 0)
    {
        backgroundMode = 1;
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
        fflush(stdout);
    }
    else
    {
        backgroundMode = 0;
        char* message = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
        fflush(stdout);
    }
    return;
}


/*
 *The getStatus function is based off the Exploration: Process API - Monitoring Child Processes.
 *  When called, it returns the exist status of the last foreground process.
 */
void getStatus(int statusFlag)
{
    if (WIFEXITED(statusFlag))
    {
        printf("exit value %d\n", WEXITSTATUS(statusFlag));
        fflush(stdout);
    }
    else
    {
        printf("terminated by signal %d\n", statusFlag);
        fflush(stdout);
    }
}

/*
 * The changeDirectory function determines which directory to change to. If only "cd" is entered, the directory is
 *    changed to the users "HOME" directory. If the users specified a target directory, the changeDirectory function
 *    will attempt to navigate to that directory.
 */
void changeDirectory(char** args, int argsIndex)
{
    char* targetDir;

    if (argsIndex == 1)
    {
        chdir(getenv("HOME"));
    }
    else
    {
        targetDir = args[1];
        if (chdir(targetDir) != 0)
        {
            printf("Directory name is invalid.\n");
        }
    }
    return;
}

/*
 *The expand function performs the variable expansion of "$$" into the current pid of the running process.
 *   The function returns the provided string
 */
char* expand(char* input, char* orig, int pid)
{
    static char expandedToken[MAX_LENGTH];
    char* ptr;

    // initialize the ptr variable by determining the first instance of orig ("$$")
    if (!(ptr = strstr(input, orig)))
        return input;

    // copy everything up to the ptr to into expandedToken
    strncpy(expandedToken, input, ptr - input);
    expandedToken[ptr - input] = '\0';

    // append the pid and the remainder of the string into expandedToken
    sprintf(expandedToken + (ptr - input), "%d%s", pid, ptr + strlen(orig));

    return expandedToken;
}

/*
 *The processCommand function is responsible for the parsing of input from the user and routing commands to the
 *   appropriate handler.
 *
 *   The format of the accepted commands are:
 *       COMMAND FORMAT:  command [arg1 arg2 ...] [< input_file] [> output_file] [&]
 */
int processCommand(char* command[])//, struct sigaction saSigint, struct sigaction sigtstpAction, int *statusFlag)
{
    // allocate memory for the command string processing
    char* token = calloc(strlen(command) + 1, sizeof(char));
    char* savePTR;
    char* expansion = calloc(strlen(command) + 1, sizeof(char));
    char* saveEXP;

    // Define the args array to hold the command and arguements
    char* args[MAX_ARGS] = { NULL };
    int argsIndex = 0;

    // Define the files needed in the event input and/or output files are specified.
    char* inFile = calloc(strlen(command) + 1, sizeof(char));
    char* outFile = calloc(strlen(command) + 1, sizeof(char));

    // backgroundFlag is used to track a trailing "&" indicating a background process
    int backgroundFlag = 0;
    int forkReturn;

    // set forkPID to an impossible value
    pid_t spawnPID = -3;
    pid_t exitedPID;

    // copy command into token to preserve the initial string entered by the user.
    strcpy(token, command);
    
    // Remove the new line from the input.
    token = strtok_r(token, "\n", &savePTR);
    
    // Ignore comments and blanklines.
    if (strcmp(token, "") == 0 || strcmp(token, "#") == 0 || token[0] == '#')
    {
        free(inFile);
        free(outFile);
        free(token);
        return 0;
    }
    else
    {
        // loop through the token containing the command and expand all instances of "$$"
        while (strstr(token, "$$") != NULL)
        {
            int pid = getpid();
            expansion = expand(token, "$$", pid);
            strcpy(token, expansion);
        }
        // a second check just for the case of only a "#" entered.
        token = strtok_r(token, " ", &savePTR);
        if (strcmp(token, "#") == 0)
        {
            free(token);
            free(inFile);
            free(outFile);
            return 0;
        }
        else
        {
            // Loop through the token and add each arguemement to the argsArray
            while (token != NULL)
            {
                // if a ">" is identified, the next token is the desired output file so that is stored and continue processing.
                if (strcmp(token, ">") == 0)
                {
                    token = strtok_r(NULL, " ", &savePTR);
                    sprintf(outFile, "%s", token);
                    token = strtok_r(NULL, " ", &savePTR);
                    continue;
                }
                // if a "<" is identified, the next token is the desired input file so that is stored and continue processing.
                else if (strcmp(token, "<") == 0)
                {
                    token = strtok_r(NULL, " ", &savePTR);
                    inFile = strdup(token);
                    token = strtok_r(NULL, " ", &savePTR);
                    continue;
                }
                else
                {
                    args[argsIndex] = strdup(token);
                    argsIndex++;
                    token = strtok_r(NULL, " ", &savePTR);
                }
            }

            // an "&" indicating the process should be run in the background will always be in the last position of the
            //  args array. If it is found, we remove it and set eh background flag appropriately.
            if (strcmp(args[argsIndex - 1], "&") == 0)
            {
                argsIndex--;
                args[argsIndex] = NULL;
                if (backgroundMode == 0) 
                {
                    backgroundFlag = 1;
                }
                else
                {
                    backgroundFlag = 0;
                }
                
            }

            // in the case of a cd command, we route it to the built-in change directory function.
            if (strcmp(args[0], "cd") == 0)
            {
                changeDirectory(args, argsIndex);
            }

            // in the case of an exit, we iterate over the stored process IDs and kill them individually before exiting.
            else if (strcmp(args[0], "exit") == 0)
            {
                int i = 0;
                while (processArray[i] != NULL)
                {
                    kill(processArray[i], 9);
                    i++;
                }
                exit(0);
            }
            // in the case of a status command, we route it to the built-in status function.
            else if (strcmp(args[0], "status") == 0)
            {
                getStatus(statusFlag);
            }
            // Otherwise we fork the process to run execvp() for all other functions besides the built-ins.
            else
            {
                spawnPID = fork();

                switch (spawnPID)
                {
                    // -1 means there was an error with the fork.
                    case -1:
                    {
                        perror("fork() failed!");
                        exit(1);
                        statusFlag = 1;
                        break;
                    }
                    // 0 means this is the child process.
                    case 0:
                    {
                        // if the background flag is 0, we update the sigint handler.
                        if (backgroundFlag == 0)
                        {
                            sigint.sa_handler = SIG_DFL;
                            sigaction(SIGINT, &sigint, NULL);
                        }
                        // if no output file is specified and this is a background process, we must write to /dev/null
                        //    to prevent the output of the command from being displayed on screen.
                        if (strcmp(outFile, "") == 0 && backgroundFlag == 1)
                        {
                            int outputFile = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                            if (outputFile == -1)
                            {
                                printf('%s: no such file or directory\n', args[0]);
                                fflush(stdout);
                                exit(1);
                            }
                            if (dup2(outputFile, 1) == -1)
                            {
                                printf("failed to output to %s\n", outFile);
                                fflush(stdout);
                                exit(1);
                            }
                            close(outputFile);
                        }
                        // If an output file is specified, we output results of the command to the output file.
                        if (strcmp(outFile, "") != 0)
                        {
                            int outputFile = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                            if (outputFile == -1)
                            {
                                printf('%s: no such file or directory\n', args[0]);
                                fflush(stdout);
                                exit(1);
                            }
                            if(dup2(outputFile, 1) == -1)
                            {
                                printf("failed to output to %s\n", outFile);
                                fflush(stdout);
                                exit(1);
                            }
                            close(outputFile);
                        }
                        
                        // If an input file is specified, we must first open the file for reading.
                        if (strcmp(inFile, "") != 0)
                        {
                            int inputFile = open(inFile, O_RDONLY);
                            if (inputFile == -1)
                            {
                                printf("cannot open %s for input\n", inFile);
                                fflush(stdout);
                                exit(1);
                            }
                            if (dup2(inputFile, 0) == -1)
                            {
                                printf("failed to open %s, file cannot be found\n", inFile);
                                fflush(stdout);
                                exit(1);
                            }
                            close(inputFile);
                        }
                        // execute the command leveraging the PATH environment variable.
                        forkReturn = execvp(args[0], args);
                        // if the value in forkReturn is less that 0, that means the execution failed.
                        if (forkReturn < 0)
                        {
                            printf("%s: no such file or directory\n", args[0]);
                            fflush(stdout);
                            exit(1);
                        }
                        break;
                    }

                    default:
                    {
                        /* if backgroundFlag == 1, then the command specified that it should be run in the background.
                         *  backgroundMode == 0 means that background processes are allowed.
                         *  Therefore we run the command, print it's PID to the user, and track the PID for use in later exit functions..
                         */
                        if (backgroundFlag == 1 && backgroundMode == 0)
                        {
                            processArray[processIndex] = spawnPID;
                            processIndex++;
                            pid_t newPID = waitpid(spawnPID, &statusFlag, WNOHANG);
                            printf("background pid is %d\n", spawnPID);
                            fflush(stdout);
                        }

                        else
                        {
                            pid_t newPID = waitpid(spawnPID, &statusFlag, 0);
                        }
                    }

                    // if background mode is enabled, we check to see if processes have finished and if so we 
                    //  display the process id before we move on to the next prompt. 
                    if (backgroundMode == 0){
                        spawnPID = waitpid(-1, &statusFlag, WNOHANG);
                        while (spawnPID > 0)
                        {
                            printf("background pid %i is done: ", spawnPID);
                            getStatus(statusFlag);
                            spawnPID = waitpid(-1, &statusFlag, WNOHANG);
                            fflush(stdout);
                        }
                    }
                }
            }
        }
        // we clean up the inputfile, outputfile, and token variables before we processed to the next command.
        free(inFile);
        free(outFile);
        free(token);
        return 0;
    }
}

/*
 *The main function is responsible for managing the retreval of the input from the user and passing that array to the
 *   process command function.
 */

int main() {
    
    // handle SIGINT signal
    sigint.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sigint, NULL);
    sigfillset(&sigint.sa_mask);
    sigint.sa_flags = 0;

    // handle SIGTSTP signal
    sigtstp.sa_handler = catchSIGTSTP;
    sigfillset(&sigtstp.sa_mask);
    sigtstp.sa_flags = 0;
    sigaction(SIGTSTP, &sigtstp, NULL);

    char command[MAX_LENGTH];

    while (1) {
        // print instruction prompt and get string from user.
        printf(": ");
        fflush(stdout);

        fgets(command, MAX_LENGTH, stdin);
        if (command[0] == '\n')
        {
            continue;
        }
        else
        {
            processCommand(command);
        }
    }
}
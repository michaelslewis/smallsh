/*******************************************************************
* Michael S. Lewis
* CS 344 Fall 2017
* Program 3: smallsh
********************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

/* Preprocessor directives (to expand with constants). */
#define MAX_CHARS 2048      // Maximum length of command line shell will support.
#define MAX_ARGS 512        // Maximum number of arguments shell will support.

/* Organizes attributes for any parsed input. */
struct parsedInput
{
    bool backMode;                  // Indicates a background process is active when true.
    char inputFile[128];            // Stores string for reading from input file.
    char outputFile[128];           // Stores string for writing to output file.
    char shellCommand[MAX_CHARS];   // Stores string for a command.
    int argNum;                     // Counts number of arguments within a parsed line.
    char *arguments[MAX_ARGS];      // Stores arguments from parsed input.
};

/* Process ID stack is used for managing background processes. */
struct processStack
{
    int backPidNum;             // Counts background processes.
    pid_t backPids[MAX_ARGS];   // Stores each ID of background processes.
};

/* Global variables. */
struct processStack pidStack;   // Instantiates the processStack.
int foregroundValue;            // Indicates exit status or signal used to terminate.
bool foregroundMode = false;    // Indicates when foreground mode is enabled. Initiated as disabled.

/* Function declarations. */
void removeBackPid(pid_t processId);
int changeDir(char* inputBuffer);
bool testForOperator(char *str);
void parseForStrings(char* inputBuffer, struct parsedInput* obj);
void makeArgs(struct parsedInput* obj, char** argsArray);
void redirectIO(struct parsedInput* obj);
void forkProcesses(struct parsedInput* obj);
void trapStopSig(int sig);
void trapChildSig(int sig);
void trapTermSig(int sig);
void resetInput(struct parsedInput* obj);
void endProcess();
void testBackMode();


int main()
{
    char inputBuffer[MAX_CHARS];    // Stores input from stdio.
    struct parsedInput *obj;        // Instantiates the parsedInput.
    int fgStatus;                   // Stores exit status or signal used to terminate.
    int i;                          // Index for loop.
    char *tempdir1;                 // Handles source for pid variable parsing.
    char *tempdir2;                 // Handles destination for pid variable parsing.
    char *pidVar;                   // Stores pid value for string.

    /* Creates stack of background process ID's. */
    pidStack.backPidNum = -1;
    for(i = 0; i < MAX_ARGS; i++){
        pidStack.backPids[i] = -1;  // Initializes each value in backPids to -1.
    }

    /* Initializes Stop signal. */
    struct sigaction StopSignal;
    StopSignal.sa_handler = trapStopSig;
    StopSignal.sa_flags = 0;

    /* Initializes Terminate signal. */
    struct sigaction TermSignal;
    TermSignal.sa_handler = trapTermSig;
    StopSignal.sa_flags = 0;

    /* Initializes Child signal. */
    struct sigaction ChildSignal;
    ChildSignal.sa_handler = trapChildSig;
    StopSignal.sa_flags = 0;

    /* Loop manages signal handlers, shell built-ins, and prompt for command line. */  
    do
    {
        /* Resets signal handlers. */
        sigaction(SIGCHLD, &ChildSignal, NULL);
        sigaction(SIGTSTP, &StopSignal, NULL);
        sigaction(SIGINT, &TermSignal, NULL);

        usleep(3000);   // Wait state avoids a race condition between trapping signals and subsequent commands.

        testBackMode();   // If a stop signal is caught, the foreground mode is switched.

        /* Flushes the buffer to clear stdout and stdin. */
        fflush(stdout);
        fflush(stdin);

        /* Prints a colon as the prompt. */
        printf(": ");
        memset(inputBuffer, '\0', sizeof(inputBuffer));
        fgets(inputBuffer, sizeof(inputBuffer), stdin); // Gets user input to command line.
        
        /* Flushes the buffer to clear stdout and stdin. */
        fflush(stdout);
        fflush(stdin);

        /* Handles built-in commands and special characters. */
        if(strncmp(inputBuffer, "exit", 4) == 0){ // Recognizes "exit" command and exits from shell.
            endProcess();
            exit(0);
        }
        if((pidVar = strstr(inputBuffer, "testdir$$")) != NULL) {   // Handles expansion of pid variable.
            int replacePID = (int)getpid();     // Stores current pid.
            tempdir1 = malloc(sizeof(char) * 8);    // Allocates memory for string to hold source.
            tempdir2 = malloc(sizeof(char) * 20);   // Allocates memory for string to hold destination.
            strcpy(tempdir1, "testdir");            // Builds string to hold test directory.
            sprintf(tempdir2, "%s%d", tempdir1, replacePID);    // Stores concatenated testdir and pid.
            char* tempString = malloc(sizeof(char) * strlen(inputBuffer));  // Allocates memory for string.
            *pidVar = 0;    // Clears pidVar.
            strcpy(tempString, inputBuffer);    // Copies inputBuffer as string.
            sprintf(inputBuffer, "%s%s", tempString, tempdir2); // Replaces inputBuffer with final string.
            free(tempString);   // Frees and de-allocates tempString memory.
            tempString = NULL;
            strtok(inputBuffer, "$"); // Truncates pid variable from argument string.
        }
        if(strncmp(inputBuffer, "#", 1) == 0){ // Recognizes '#' character and ignores comment.
            continue;
        }
        else if(strncmp(inputBuffer, "\n", 1) == 0){ // Recognizes empty line and proceeds to next line.
            continue;
        }
        else if(strncmp(inputBuffer, "cd", 2) == 0){ // Recognizes "cd" command to change directories.
            changeDir(inputBuffer);
        }
        else if(strncmp(inputBuffer, "status",6) == 0){ // Tests for last exit value from foreground.
            if(WEXITSTATUS(foregroundValue)){
                fgStatus = WEXITSTATUS(foregroundValue);    // Tests if process exited.
            }
            else{
                fgStatus = WTERMSIG(foregroundValue); // Tests if process was terminated by a signal.
            }
            printf("exit value %d\n", fgStatus);
        }
        else{
            if(inputBuffer != NULL && strcmp(inputBuffer, "") != 0){
                /* Reads input to parse for strings. */
                obj = malloc(1 * sizeof(struct parsedInput)); 
                parseForStrings(inputBuffer, obj); // Parses input.

                forkProcesses(obj); // Runs commands and manages parent and child processes.

                resetInput(obj);    // Frees memory and clears the input object.
            }
            else{
                continue;   // Proceeds through loop again while true.
            }
        }
    } while(true);

    return 0;
}


/*******************************************************************
 * Name: void removeBackPid(pid_t processId)
 * Description: Removes the process ID if a prior background
 *              process ends.
 * Arguments: A pid_t for the process ID.
 *******************************************************************/
void removeBackPid(pid_t processId)
{
    int i;      // Index for loop.
    int pidPos; // Stores position of process ID in stack.

    /* Locates process ID in stack that matches background process that ended. */
    for(i = 0; i < pidStack.backPidNum + 1; i++){
        if(pidStack.backPids[i] == processId){
            pidPos = i;
            break;  // Exits for loop once process ID is located.
        }
    }

    /* Reorders remaining each remaining process ID in stack. */
    for(i = pidPos; i < pidStack.backPidNum + 1; i++){
        pidStack.backPids[i] = pidStack.backPids[i+1];
    }
    
    pidStack.backPidNum--;  // Decrements process ID count after pid is removed.
}


/*******************************************************************
 * Name: int changeDir(char* inputBuffer)
 * Description: Facilitates operations to change and navigate
 *              from the working directory.
 * Arguments: Pointer to char for buffer to store user input.
 *******************************************************************/
int changeDir(char* inputBuffer)
{
    char* homePath = getenv("HOME"); // Gets path to home directory.
    char newPath[MAX_CHARS];    // Stores string for name of specified directory path.

    inputBuffer[strlen(inputBuffer) -1] = '\0';

    if(strcmp(inputBuffer,"cd") == 0){
        if(chdir(homePath) != 0){ // Returning anything but 0 means directory not found.
            printf("Directory:%s not found.\n", homePath);
            return 1;
        }
        return 0;
    }

    memset(newPath, '\0', sizeof(newPath)); // Clears out string for name of specified directory path.

    strtok(inputBuffer, " "); // Removes extraneous spaces.
    strcpy(inputBuffer, strtok(NULL, ""));
    
    /* Directory commands. */
    if(inputBuffer[0] == '/'){
        sprintf(newPath, "%s%s", homePath, inputBuffer); // Navigates to a specifed directory from home directory.
    }
    else if(strcmp(inputBuffer, "..") == 0){ // Navigates back by one folder.
        strcpy(newPath, inputBuffer);
    }
    else if(strcmp(inputBuffer, "~") == 0){ // Navigates to home directory.
        strcpy(newPath, homePath);
    }
    else if(inputBuffer[0] == '.' && inputBuffer[1] == '/'){ // Stays in current directory.
        sprintf(newPath, "%s", inputBuffer);
    }
    else{
        sprintf(newPath, "%s", inputBuffer); // Navigates to specified directory from home directory path.
    }
    if(chdir(newPath) != 0){ // Handles a directory not found.
        printf("Directory:%s not found.\n", newPath);
        return 1;
    }
    return 0;
}


/*******************************************************************
 * Name: bool testForOperator(char *str)
 * Description: Tests for '&', '<', '>', or '#'. If the char is any
 *              of these, returns bool as true.
 * Arguments: Pointer to char to test input.
 *******************************************************************/
bool testForOperator(char *str)
{
    bool shellChar = false; // Indicates if the character has operator functionality for the shell.

    if(str == NULL){ // Tests for null value to prevent segmentation fault for remaining conditions.
        return true;
    }

    if(str[0] == '&'){ // Tests for bg mode character.
        shellChar = true;
    }
    else if(str[0] == '<'){ // Tests for input redirection character.
        shellChar = true;
    }
    else if(str[0] == '>'){ // Tests for output redirection character.
        shellChar = true;
    }
    else if(str[0] == '#'){ // Tests for comment character.
        shellChar = true;
    }
    return shellChar;
}


/*******************************************************************
 * Name: void parseForStrings(char* inputBuffer, struct parsedInput* obj)
 * Description: Initializes the parsedInput struct with any args.
 * Arguments: Pointer to parsedInput struct and pointer to char to
 *            parse input.
 *******************************************************************/
void parseForStrings(char* inputBuffer, struct parsedInput* obj)
{
    char dataBuffer[MAX_CHARS]; // Temporary buffer for storing contents of inputBuffer.
    char *inputFileName;        // Stores descriptor for input file.
    char *outputFileName;       // Stores descriptor for output file.
    char *temp;                 // Stores argument copied from the data buffer.

    obj->argNum = 0;
    inputBuffer[strlen(inputBuffer) -1] = '\0'; // Removes newline character.

    if(inputBuffer[strlen(inputBuffer) -1] == '&'){ // Tests for background mode.
        obj->backMode = true;                       // Background mode is already enabled.
        inputBuffer[strlen(inputBuffer) -1] = '\0'; // Ignores and removes the ampersand.
    }
    else{
        obj->backMode = false;    // Background mode is not enabled.
    }

    /* Parses a command from input and places in command array. */
    memset(dataBuffer, '\0', sizeof(dataBuffer)); // Clears out dataBuffer.
    strcpy(dataBuffer, inputBuffer); // Copies contents of inputBuffer to dataBuffer.
    strtok(dataBuffer, " "); // Grabs only the command portion of input.
    strcpy(obj->shellCommand, dataBuffer); // Copies command input into new shellCommand object.

    /* Parses input file name. */
    memset(dataBuffer, '\0', sizeof(dataBuffer)); // Clears out dataBuffer.
    strcpy(dataBuffer, inputBuffer);    // Copies inputBuffer into dataBuffer.
    inputFileName = strstr(dataBuffer, "<"); // Locates substring after input redirection operator.
    if(inputFileName != NULL){
        memmove(inputFileName, inputFileName+2, strlen(inputFileName)); // Copies memory block of string, minus operator.
        strtok(inputFileName, " "); // Truncates space from input file name string.
        inputFileName[strlen(inputFileName)] = '\0'; // Terminates string with null character. 
        strcpy(obj->inputFile, inputFileName);  // Copies string into inputFile object.
    }

    /* Parses output file name. */
    memset(dataBuffer, '\0', sizeof(dataBuffer));   // Clears out dataBuffer.
    strcpy(dataBuffer, inputBuffer);                // Copies inputBuffer into dataBuffer.
    outputFileName = strstr(dataBuffer, ">");       // Locates substring after output redirection operator.
    if(outputFileName != NULL){
        memmove(outputFileName, outputFileName+2, strlen(outputFileName));  // Copies memory block of string, minus operator.
        strtok(outputFileName, " ");                    // Truncates space from output file name string.
        outputFileName[strlen(outputFileName)] = '\0';  // Terminates string with null character. 
        strcpy(obj->outputFile, outputFileName);        // Copies string into outputFile object.
    }
     
    /* Parses arguments. */
    memset(dataBuffer, '\0', sizeof(dataBuffer));   // Clears out dataBuffer.
    strcpy(dataBuffer, inputBuffer);    // Copies inputBuffer into dataBuffer.
    strtok(dataBuffer, " ");    // Grabs only the argument portion of input before space.

    temp = strtok(NULL, "");    // Grabs only the argument portion of input after space.

    if(testForOperator(temp) == false){ // Tests if any arguments are present.
        strcpy(dataBuffer, temp);   // Copies stored argument portion into dataBuffer.
        strtok(dataBuffer, "<>#"); // Grabs any special characters before arguments.
        
        strtok(dataBuffer, " "); // Truncates space from argument string.
        obj->arguments[0] = dataBuffer; // Stores contents of dataBuffer as first argument.
        obj->argNum = 1;   // Identifies the first argument entered.
        temp = strtok(NULL, " "); // Prepares to recognize any subsequent arguments in input.
        while(temp != NULL){
            
            obj->arguments[obj->argNum] = temp;    // Stores remaining arguments.
            obj->argNum++;                         // Increments argument count.
            temp = strtok(NULL, " ");   // Prepares to recognize any subsequent arguments in input.
        }
        obj->arguments[obj->argNum] = strtok(NULL, "");    // Grabs the final argument as object attribute.
    }
}


/*******************************************************************
 * Name: void makeArgs(struct parsedInput* obj, char** argsArray)
 * Description: Generates list of arguments that can later be
 *              passed to execvp.
 * Arguments: A pointer to an array of pointers and a pointer to a
 *            parsedInput struct.
 *******************************************************************/
void makeArgs(struct parsedInput* obj, char** argsArray)
{
    int i;  // Index for loop.

    argsArray[0] = obj->shellCommand; // Stores the command as the initial argument.
    for(i = 0; i < obj->argNum ; i++){ // Loops through all argsArray.
        if(getenv(obj->arguments[i]) != NULL){  // Tests if environment variable is not null.
            argsArray[i+1] = getenv(obj->arguments[i]); // Adds as subsequent argument to list.
        }
        else if(strcmp(obj->arguments[i], "$$") == 0){  // Tests for process ID variable.
            sprintf(argsArray[i+1], "%d", getpid());         // Expands to actual pid value.
        }
        else{
            argsArray[i+1] = (obj->arguments[i]);    // Adds current argument object as subsequent argument to list.
        }
    }

    argsArray[i+1] = NULL; // Terminates argsArray array.
}


/*******************************************************************
 * Name: void redirectIO(struct parsedInput* obj)
 * Description: Handles input and output redirection.
 * Arguments: A pointer to an parsedInput struct.
 *******************************************************************/
void redirectIO(struct parsedInput* obj)
{
    int inputFileDescriptor = STDIN_FILENO;
    int outputFileDescriptor = STDOUT_FILENO;

    if(obj->inputFile[0] != '\0'){  // Tests if input file is empty.
        /* Attempts to open input file. */
        inputFileDescriptor = open(obj->inputFile, O_RDONLY);

        if(inputFileDescriptor < 0){    // Tests for valid return value from opening file.
            printf("cannot open file for input\n");   // Prints error message and exits, as needed.
            exit(1);
        }
        dup2(inputFileDescriptor, 0);   // Duplicates file descriptor to redirect to stdin.
        close(inputFileDescriptor);     // Closes the file.
    }
    if(obj->outputFile[0] != '\0'){ // Tests if output file is empty.
        /* Attempts to open output file for creation or truncation, and assigns permission value. */
        outputFileDescriptor = open(obj->outputFile,O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if(outputFileDescriptor < 0){   // Tests for valid return value from opening file.
            printf("Error opening or creating file\n");  // Prints error message and exits, as needed.
            exit(1);
        }

        dup2(outputFileDescriptor, 1);  // Duplicates file descriptor to redirect to stdout.
        close(outputFileDescriptor);    // Closes the file.
    }
}


/*******************************************************************
 * Name: void forkProcesses(struct parsedInput* obj)
 * Description: Creates a fork for a child process.
 * Arguments: A pointer to an parsedInput struct.
 *******************************************************************/
void forkProcesses(struct parsedInput* obj)
{
    pid_t pid = fork();
    pid_t topBackPid;   // Stores top of stack.
    char *argList[MAX_ARGS];
    int processValue;

    switch(pid)
    {
        /* If value returns as -1, then there was an error in the fork. */
        case -1:
            printf("Something went wrong with fork()\n");
            exit(1);
            break;

        /* If value returns as 0, then this is a child process. */
        case 0:
            redirectIO(obj);    // Handles input and output redirection for obj.
            makeArgs(obj, argList); // Generates list of arguments with obj.
            execvp(obj->shellCommand, argList); // Replaces current process with command from obj.
            printf("%s: No such file or directory\n", argList[0]);
            exit(1);
            break;

        /* Otherwise, this is a parent process. */
        default:
            if(obj->backMode == true && foregroundMode == false){ // Identifies background mode.

                /* Adds the process ID for a background process to the process ID stack. */
                pidStack.backPids[++(pidStack.backPidNum)] = pid;

                /* Reads the process ID from the top of the stack, returning its pid. */
                topBackPid = pidStack.backPids[pidStack.backPidNum];
                printf("Background pid is %d\n", topBackPid);
            }
            else{
                /* Waits for completion of child process with pid if background mode not enabled. */
                waitpid(pid, &processValue, 0);
                foregroundValue = processValue;
            }
            break;
    }
}


/*******************************************************************
 * Name: void trapStopSig(int sig)
 * Description: Handles the stop signal (^z) to enter or exit from
 *              foreground mode.
 * Arguments: An int representing the signal.
 *******************************************************************/
void trapStopSig(int sig)
{
    /* Enters foreground mode. */
    if(foregroundMode == false){
        char* message = ("\nEntering foreground-only mode (& is now ignored)\n");
        write(STDOUT_FILENO, message, 50);
        foregroundMode = true;  // Updates status of foreground mode global variable.
    }
    /* Exits from foreground mode. */
    else{
        char* message = "\nExiting foreground-only mode\n"; // exit Fg mode.
        write(STDOUT_FILENO, message, 31);
        foregroundMode = false; // Updates status of foreground mode global variable.
    }
}


/*******************************************************************
 * Name: void trapChildSig(int sig)
 * Description: Handles signal when a child process is ending.
 * Arguments: An int representing the signal.
 *******************************************************************/
void trapChildSig(int sig)
{
    pid_t childPid;
    int childStatus;
    int i;

    /* Loops through stack to find ID of process that exited or was terminated. */
    for(i = 0; i < pidStack.backPidNum + 1; i++){
        childPid = waitpid(pidStack.backPids[i], &childStatus, WNOHANG); // Identifies process ID of the child.

        if((childStatus == 0 || childStatus == 1) && childPid != 0 ){ // Handles messaging if process exited.
            fprintf(stdout, "\nBackground pid %d is done: exit value %d\n", childPid,childStatus);
            removeBackPid(childPid);    // Removes the process ID of the child from the stack.
        }
        else if(childPid != 0){ // Handles messaging if process was terminated.
            fprintf(stdout, "\nBackground pid %d is done: terminated by signal %d\n", childPid, childStatus);
            removeBackPid(childPid);    // Removes the process ID of the child from the stack.
        }
    }
}


/*******************************************************************
 * Name: void trapTermSig(int sig)
 * Description: Handles the terminate signal (^c).
 * Arguments: An int representing the signal.
 *******************************************************************/
void trapTermSig(int sig)
{
    printf("\nterminated by signal %d\n", sig); // Indicates signal that terminated the process.
}


/*******************************************************************
 * Name: void resetInput(struct parsedInput* obj)
 * Description: Frees memory from the input object.
 * Arguments: A pointer to an inputobj struct.
 *******************************************************************/
void resetInput(struct parsedInput* obj)
{
    obj->backMode = false; // Resets the background status (false is default).

    /* Fully clears each parsedInput attribute with null characters. */
    memset(obj->inputFile, '\0', sizeof(obj->inputFile));
    memset(obj->outputFile, '\0', sizeof(obj->outputFile));
    memset(obj->shellCommand, '\0', sizeof(obj->shellCommand));

    free(obj);  // De-allocates object memory.
}


/*******************************************************************
 * Name: void endProcess()
 * Description: Facilitates exiting by interupting any remaining
 *              background processes.
 * Arguments: None.
 *******************************************************************/
void endProcess()
{
    int i;  // Index for loop.

    /* Loops through and interrupts all background processes in stack. */
    for(i = 0; i < pidStack.backPidNum + 1; i++){
        kill(pidStack.backPids[i], SIGINT);
        usleep(2000);   // Wait state avoids a race condition between trapping signals and subsequent commands.
    }
}


/*******************************************************************
 * Name: void testBackMode()
 * Description: If a stop signal is caught, the foreground mode is
 *              switched.
 * Arguments: None.
 *******************************************************************/
void testBackMode()
{
    /* Exits from foreground mode if already in mode when a stop signal is caught. */
    if(WTERMSIG(foregroundValue) == 11 && foregroundMode == true){
        printf("\nExiting foreground-only mode\n");
        foregroundMode = false; // Toggles foreground mode status to false.
    }
    /* Enters foreground mode if not already in mode when a stop signal is caught. */
    else if(WTERMSIG(foregroundValue) == 11 && foregroundMode == false){
        printf("\nEntering foreground-only mode (& is now ignored)\n");
        foregroundMode = true;  // Toggles foreground mode status to true.
    }
}
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <string.h>
#include <stdbool.h>
#define MAX_COMMAND_LENGTH 1024
#define MAX_NUM_ARGS 64

// declare the pointer to the txt file of the logs
FILE *termination_logs_file;

char *remove_double_quotes(char *str)
{
    // if we have a double ended quotes string then we have to get rid of these quotes
    // otherwise return the string as it is
    if (str[0] == '"' && str[strlen(str) - 1] == '"')
    {
        str[strlen(str) - 1] = '\0';
        return str + 1;
    }
    return str;
}

char *set_up_command(char *command)
{
    // function to remove the leading and trailing spaces of a command string(input);
    while (command[0] == ' ')
    {
        command = command + 1;
    }
    while (command[strlen(command) - 1] == ' ')
    {
        command[strlen(command) - 1] = '\0';
    };
    return command;
}

void export(char str[])
{
    // this function is called when the export command is entered
    // it adds the new variable with its associated value to the environment variables
    // uses strsep() to seperate the string at the '=' sign and the variable is returned
    // then str is now pointing to whatever after '=' so i've used strdup()
    // to make a duplicate of what's remaining (value of variable).
    char *variable;
    char *value;
    variable = strsep(&str, "=");
    value = strdup(str);
    setenv(variable, remove_double_quotes(value), 1);
}

char *replace_env(char *str, int found_at)
{
    // this function takes on the command string and then sees what's after '$' and
    // this is assumed to be a variable stored in the environment variables
    // so if the variable is stored then it's value is plugged in the string
    // otherwise the "$varaible" are removed form the string

    char temp[50]; //to hold the variable found after '$' in t str
    char result[1024];// to hold the string after we evaluated the '$' if found in env 

    int count = 0;

    char *val;
    bool variable_found_in_environment = false;
    for (int i = found_at + 1; i < strlen(str); i++)
    {
        if (str[i] != '$' && str[i] != ' ' && str[i] != '"')
            temp[count++] = str[i];
        else
            break;
    }
    temp[count++] = '\0';

    if (getenv(temp) != NULL)
    {
        variable_found_in_environment = true;
        val = getenv(temp);
    }

    int it = 0, i = 0;
    while (i < strlen(str))
    {
        if (i < found_at)
        {
            result[it++] = str[i++];
        }
        else if (i == found_at)
        {
            if (variable_found_in_environment)
            {
                for (int j = 0; j < strlen(val); j++)
                {
                    result[it++] = val[j];
                }
            }
            i += strlen(temp) + 1;
        }
        else
        {
            result[it++] = str[i++];
        }
    }
    result[it++] = '\0';
    strcpy(str, result);
    return str;
}


char *send_str_to_replace(char *str)
{
    // it's a recursive function that's called whenever there's still '$' in the command
    // when '$' is encountered then we call the replace_env() with the index of '$' passed
    // recursively, the '$' signs are removed untill the command is '$' free then guess what !
    // that's the base case so we're done
    bool interpolation_found = false;
    int i;
    for (i = 0; i < strlen(str); i++)
    {
        if (str[i] == '$')
        {
            interpolation_found = true;
            break;
        }
    }
    if (interpolation_found)
    {
        char *arr = replace_env(str, i);
        return send_str_to_replace(arr);
    }
    else
        return str;
}

void *extracting_args(char command[], char *args[MAX_NUM_ARGS])
{
    // this function calls the recursive function above at the beggining to remove all '$'
    // and then we have our command ready and clean without these disturbing '$'
    // so, we slice our string into array of strings called char *args[]
    // based on the first word of the comand {cd, ls, etc..} the slicing of the remaining string is defined
    char *well_defined_command = send_str_to_replace(command);
    strcpy(command, well_defined_command);
    int count = 0;
    args[count++] = strsep(&command, " "); // get the key word for the command

    // if we have echo or export with double quotes then we need to have all of what's left in one take
    if ((strcmp(args[0], "export") == 0 && strchr(command, '"') && command[strlen(command) - 1] == '"') || strcmp(args[0], "echo") == 0)
    {
        args[count++] = command;
    }
    // otherwise we split the string to substrings and the delimeter is the space in the command
    else
    {
        while (command != NULL)
        {
            args[count++] = strsep(&command, " ");
        }
    }
}

void execute_command(char *args[], bool background_encountered)
{
    // function to execute non builtin commands
    // by forking a new child prcess which executes the execvp() function

    int status;         // int variable to indicate the exit status of child process
    pid_t pid = fork(); // create a child process

    if (pid == 0)
    {
        // child thread of execution is here
        execvp(args[0], args);
        // if you reach here then there's an error in the execvp() so we'll print out an error message
        printf("Error: command not found\n");
        exit(1);
    }
    else if (pid > 0)
    {
        // parent thread of execution is here
        // we'll condition now on whether the command is execued in the background or not
        // if it is then we'll send the option WHOHANG to the waitpid() as a parameter
        // to prevent parent blocking
        if (background_encountered)
            waitpid(pid, &status, WNOHANG);

        // other wise the parent waits for his child of course!
        else
            waitpid(pid, &status, 0);
    }

    else
    {
        // fork failed
        perror("failed to fork a new child");
        exit(1);
    }
}

void cd_handler(char *args[])
{
    // function to handle the various cases required for cd like {cd, cd .., cd ~, cd {abs or relative path}}
    char *path = args[1];
    if (path == NULL || (path[0] == '~' && strlen(path) == 1))
    {
        chdir(getenv("HOME"));
    }

    else if (path[0] == '~')
    {
        char *home_path = getenv("HOME");
        char *appended_path = path + 1;
        char dest[254];
        int count = 0;
        for (int i = 0; i < strlen(home_path); i++)
        {
            dest[count++] = home_path[i];
        }
        for (int i = 0; i < strlen(appended_path); i++)
        {
            dest[count++] = appended_path[i];
        }
        dest[count] = '\0';
        if (chdir(dest))
            printf("NO SUCH FILE OR DIRECTORY\n");
    }

    else
    {
        if (chdir(args[1]))
            printf("NO SUCH FILE OR DIRECTORY\n");
    }
}

void execute_shell_bultin(char *args[])
{
    // function that handles type 0 input which is the shell built in commands{export, cd, echo}
    if (strcmp(args[0], "cd") == 0)
    {
        cd_handler(args);
    }

    else if (strcmp(args[0], "echo") == 0)
    {
        // if it's echo then remove the quotes and you're good to go
        // note the '$' is gone, we handled it
        printf("%s\n", remove_double_quotes(args[1]));
    }

    else if (strcmp(args[0], "export") == 0)
    {
        // if the keyword was export then call the export function
        //  to add a new variable to the environment
        export(remove_double_quotes(args[1]));
    }
}

void myShell()
{
    // here's where the shell infinte loop is defined
    // the loop is only broken by the exit command
    bool command_is_not_exit = true;
    do
    {
        int input_type = 0;                          // variable to indicate the type of command whether it's builtin or not
        bool background_command_encountered = false; // set true when background command encountered '&'
        char current_working_directory[1024];
        getcwd(current_working_directory, 1024);
        printf("\033[31m");
        printf(":%s >> ", current_working_directory); // priniting the awsome red current working directory
        printf("\033[0m");
        char command[MAX_COMMAND_LENGTH] = {};

        char *args[MAX_NUM_ARGS] = {};

        for (int i = 0; args[i] != NULL; i++)
        {
            // initializing the args array with NULL
            args[i] = NULL;
        }

        // get the next command form the user and store it in command string
        fgets(command, MAX_COMMAND_LENGTH, stdin);

        // terminate the command with the null character
        command[strlen(command) - 1] = '\0';

        // delete the leading and the reailing spaces form the command string
        strcpy(command, set_up_command(command));

        if (command[strlen(command) - 1] == '&')
        {
            // if we have a command that's executed in the background
            // then set the boolean
            background_command_encountered = true;
            command[strlen(command) - 1] = '\0';
        }

        // extracting the args array from the command string
        extracting_args(command, args);

        if (args[0] == NULL)
        {
            // empty command, continue loop
            continue;
        }

        if (strcmp(args[0], "cd") == 0 || strcmp(args[0], "export") == 0 || strcmp(args[0], "echo") == 0)
        {
            input_type = 0;
        }

        else if (strcmp(args[0], "exit") == 0)
        {
            command_is_not_exit = false;
        }

        else
        {
            input_type = 1;
        }

        if (input_type)
        {
            // note that the background boolean is passed as well
            // to make the right waitpid() for the parent process
            execute_command(args, background_command_encountered);
        }

        else
        {
            execute_shell_bultin(args);
        }
    } while (command_is_not_exit);
}

void setup_environment()
{
    // function to change the directory to the current working directory
    char cwd[1000];
    getcwd(cwd, 1000);
    chdir(cwd);
}

void on_child_exit()
{
    // this is called when the signal on chid exit is recieved
    // the zombie is reaped off if it's there

    int status;
    pid_t pid;

    while (1)
    {
        pid = wait3(&status, WNOHANG, (struct rusage *)NULL);
        if (pid == 0 || pid == -1)
            break;
    }

    // here we got out of the infinte loop
    // so we log that the child's terminated successfully

    termination_logs_file = fopen("/home/adel/Termination_logs.txt", "a+");
    fprintf(termination_logs_file, "Child process was terminated\n");
    fclose(termination_logs_file);
}

int main()
{
    fclose(fopen("/home/adel/Termination_logs.txt", "w"));
    signal(SIGCHLD, on_child_exit);
    setup_environment();
    myShell();
    return 0;
}

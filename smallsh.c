/* Shell Program
 * Writer: Andrew Dempsey
 * A shell program written for CS344 at Oregon State University,
 * as the portfolio project.
 *
 * The shell is not fully featured, but contains a subset of features such as cd
 * and exit builtins, the ability to execute programs, tracking and allowing background
 * processes, as well as some minimal signal processing (to ignore CTRL-Z and CTRL-C).
 *
 * As references for this code, I used the Linux Programmer's Manual, as well as the
 * modules provided at Oregon State. The teacher also provided a replace function that I
 * modified for use in the program. As well as internet searches when stuck with an error
 * I could not immediately solve.
 *
 */
 

#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <stdbool.h>
#include "replace.h"

// Define debug print
#ifdef DEBUG
#define dprintf(...) fprintf(stderr,"[DEBUG]: "__VA_ARGS__)
#else
#define dprintf(...) ((void)0)
#endif

/*
 *  Signal Handler Prototypes
 */
void handle_SIGINT(int signo) {}

/*
 * Function Prototypes
 */

// Print the prompt based on environment variable name given
// Returns count of printed chars, outputs to fprintf
int print_prompt(const char *name);

// Splits input given an environment variable name
int split_input(const char *name, char *input, char *arr[]);

// Convert PID to string and return it
// Allocates to heap for a string long enough
// Returns pointer to str, must be freed
char *pidtos(pid_t pid);
char *itos(int i);

//
// Builtin Command Prototypes
//

//
int exit_builtin(int argc, char *args[], char *fg_status);

// Builtin for implementing cd
// Given new directory string and ct of arguments
// in the words array
int cd_builtin(int argc, char *args[], char *home);

/* 
 * Structures
 */
struct PIDNums {
  pid_t smallsh_pid;
  pid_t bg_pid;
};

struct ExitStats {
  char *main;
  char *fg;
};

struct Parser {
  bool comment;
  bool bg;
  bool input;
  bool output;
};

// Global vars and constants
size_t WORD_LIMIT = 513;

int main(int argc, char *argv[]) {

  // Signal Handlers smallsh
  struct sigaction default_signals = {0};
  // Ignore CTRL-Z at all times SIGTSTP
  // Start ignoring SIGINT (CTRL-C)
  struct sigaction ignore_signal = {0};
  ignore_signal.sa_handler = SIG_IGN;
  sigaction(SIGTSTP, &ignore_signal, NULL);
  sigaction(SIGINT, &ignore_signal, &default_signals);

  // Set up the handler to stop ignoring CTRL-C
  // FOr during GETLINE input
  struct sigaction SIGINT_action = {0};
  SIGINT_action.sa_handler = handle_SIGINT;
  sigfillset(&SIGINT_action.sa_mask);
  SIGINT_action.sa_flags = 0;

  // Initialize some variables
  char *home = strdup(getenv("HOME"));
  if (!home) {
    perror("strdup() failed");
    exit(EXIT_FAILURE);
  }

  struct PIDNums *pid_nums = malloc(sizeof *pid_nums);
  if (!pid_nums) {
    perror("malloc() failed");
    exit(EXIT_FAILURE);
  }
  pid_nums->smallsh_pid = getpid();  // Init main smallsh pid
  pid_nums->bg_pid = -1;                                    
  
  //
  // Exit Status allocation
  // 
  struct ExitStats *e_status = malloc(sizeof *e_status);
  if (!e_status) {
    perror("malloc() failed");
    exit(EXIT_FAILURE);
  }
  
  // Default exit values
  e_status->main = strdup("0");
  e_status->fg = strdup("0");

  // Track input array
  int wordct = 0;
    // Define variables to get input
  char *line = NULL;
  size_t n = 0;
  ssize_t line_length = 0;

  /* 
   * MAIN LOOP for program
   * *******************************
   * Background Checking
   * Init Word Array
   * Input - Prompt and Get input
   * Word Splitting
   * Expansion
   * Parse
   * Buil-In commands (cd and exit)
   * Fork - Execute
   * 
   *********************************
   * Stop the program with the exit command
   */

  for (;;) {

    /* Background Processes */
    pid_t bgPid;
    int bgStatus;

    while ((bgPid = waitpid(0, &bgStatus, WUNTRACED|WNOHANG)) != 0) {
      if (bgPid == -1) {
        break;
      }
      
      if (WIFEXITED(bgStatus)) {
        // Exit Normally
        fprintf(stderr, "Child process %jd done. Exit Status %d.\n", (intmax_t) bgPid, WEXITSTATUS(bgStatus));
      }
        
      if (WIFSIGNALED(bgStatus)) {
        // Abnormal termination 
        fprintf(stderr, "Child process %jd done. Signalled %d.\n", (intmax_t) bgPid, WTERMSIG(bgStatus));
      }

      if (WIFSTOPPED(bgStatus)) {
        // Stopped signals
        kill(bgPid, SIGCONT);
        fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) bgPid);
     }

    }
    
    /* Initializing */
    char **words = malloc(sizeof *words * WORD_LIMIT);
    if (!words) {
      perror("malloc() failed");
      exit(EXIT_FAILURE);
    }
    for (int i = 0; i < WORD_LIMIT; i++) {
      words[i] = NULL;
    }


    /* Input */

    // Clear all errors from previous iterations
    // Not clearing stdin error causes eternal loop
    clearerr(stdin);
    
    // Prompt
    if (print_prompt("PS1") == -1) dprintf("print_prompt() error");

    // Set SIGINT signal to empty handler
    // And get input
    sigaction(SIGINT, &SIGINT_action, NULL);
    line_length = getline(&line, &n, stdin);
    if (line_length < 0) {
     // Exit on __EOF__
     if (feof(stdin)) exit(atoi(e_status->fg));

     // Reset error after empty SIGINT and error tripped
     // Unbreakable loop occurrs if error is not reset
     if (line_length == -1) {
       errno = 0;
       fprintf(stderr, "\n");
       goto exit;
     }
    }

    if (line_length == 1) goto exit;

    // Reset to ignore SIGINT in the rest of applicatio
    sigaction(SIGINT, &ignore_signal, NULL);

    /* Word splitting */
    wordct = split_input("IFS", line, words);

    // Debug messages to view words in debug build
    for (int i = 0; i < wordct + 1; i++) {
        dprintf("Word %d is %s\n", i, words[i]);
    }

    //
    // Expansion
    //
    char *sub_str;
    char *pid_str;
    char *find;
    for (int i = 0; words[i]; i++) {

      // Home Expansion
      sub_str = NULL;
      find = NULL;
      find = strstr(words[i], "~/");
      ptrdiff_t diff = find - words[i];
      if (diff == 0 && find) {
        sub_str = str_replace(&words[i], "~", home, 1);
        if (sub_str) {
          words[i] = sub_str;
        }
      }

      // Smallsh pid expansion
      sub_str = NULL;
      pid_str = NULL;
      pid_str = pidtos(pid_nums->smallsh_pid);
      if (pid_str) sub_str = str_replace(&words[i], "$$", pid_str, -1);
      free(pid_str);
      if (sub_str) {
        words[i] = sub_str;
      }

      // Expand last foreground command values ($?")
      sub_str = NULL;
      sub_str = str_replace(&words[i], "$?", e_status->fg, -1);
      if (sub_str) {
        words[i] = sub_str;
      }
      

      // Expand $! to process ID of most recent background process
      pid_str = NULL;
      sub_str = NULL;
      if (pid_nums->bg_pid > -1) {
        pid_str = pidtos(pid_nums->bg_pid);
        if (pid_str) sub_str = str_replace(&words[i], "$!", pid_str, -1);
      } else {
        pid_str = strdup("");
        sub_str = str_replace(&words[i], "$!", pid_str, -1);
      }
      if (sub_str) {
        words[i] = sub_str; 
      }
      free(pid_str);
    }  // End expansion loop

    //
    // Parsing
    // 
    struct Parser parser = {false, false, false, false}; 

    char **ptr = words;
    int last_index = 0;    
    for (int i = 0; ptr[i]; i++) {
      if (strcmp(ptr[i], "#") == 0) {
        (parser.comment) = true;
        last_index = i;
        break;
      }
      last_index = i + 1;
    }
    if (last_index>0) {
      ptr[last_index] = NULL;
      last_index--;
    }


    // Check if & is last value
    if ((strcmp(ptr[last_index], "&") == 0) && (last_index > 0)) {
      (parser.bg) = true;
      ptr[last_index] = NULL;
      last_index--;
    }
    
    // Redirection operators
    char *file = NULL;
    char *input_file = NULL;
    char *output_file = NULL;
    // Ensure only the last four array words are checked
    for (int i = 4;i > 0; i--) {
      
      // Must have 0: command, 1: redirection, 2: file
      if (last_index < 1) break;
      
      // If there is already one of each redirection, exit
      // the loop
      if (input_file && output_file) break;

      // Check for redirection character
      if (strcmp(ptr[last_index], "<") == 0 || strcmp(ptr[last_index], ">") == 0) {
        // Check for redirection type
        // And that < or > is preceded by an argument or FILE
        if (file) {
          // Check for input < redirection operator
          if (strcmp(ptr[last_index], "<") == 0) {
            input_file = strdup(file);
            (parser.input) = true;
            ptr[last_index] = NULL;
            file = NULL;
          // Check for output > redirection operator
          } else if (strcmp(ptr[last_index], ">") == 0) {
            output_file = strdup(file);
            (parser.output) = true;
            ptr[last_index] = NULL;
            file = NULL;
          }
        }
      }

      file = ptr[last_index];
      last_index--;
    }

    // Debug code if input and output file
    // Debug build only
    if (input_file) dprintf("FOUND INPUT: %s\n", input_file);
    if (output_file) dprintf("FOUND OUTPUT: %s\n", output_file);

    //
    // Execute
    //
    char *command = words[0];
    int arg_count = 0;
    for (int i = 0; words[i]; i++) {
      arg_count++;
    }
    char **args = words;
    if (args[1]) argv = &args[1];

    // Debug build code to check command name passed
    dprintf("Command is: %s", command);

    // exit builtin
    if (strcmp(command, "exit") == 0) exit_builtin(arg_count, args, e_status->fg);

    // cd builtin
    if (strcmp(command, "cd") == 0) {
      int cdres = cd_builtin(arg_count, args, home);
      if (cdres == -1) dprintf("Problem occurred when attempting cd command\n");
      goto exit;
    }

    // Fork

    int childStatus;
    pid_t childPid = fork();
    switch (childPid) {
      case -1:
        perror("fork() failed");
        exit(EXIT_FAILURE);
        break;
      case 0:
        // Child code
        sigaction(SIGINT, &default_signals, NULL);
        
        // Redirection
        if (parser.input) {
          int new_file = open(input_file, O_RDONLY|O_CREAT);
          dup2(new_file, 0);
        }

        if (parser.output) {
          int new_file = open(output_file, O_WRONLY|O_CREAT, 0777);
          int res = dup2(new_file, 1);
          if (res == -1) {
            perror("dup2()");
            exit(2);
          }
        }

        if (!args[0]) break;
        if(execvp(args[0], args) == -1) {
        perror("execve");
        exit(2);
        }
        break;

      // Parent code
      default:
        if (parser.bg == false) {
          childPid = waitpid(childPid, &childStatus, WUNTRACED);
          if(WIFEXITED(childStatus)) {
            // Set exit status of foreground process
            dprintf("Child %d exited normally\n", childPid);
            char * conv_str = itos(WEXITSTATUS(childStatus));
            free(e_status->fg);
            e_status->fg = strdup(conv_str);
            free(conv_str);
            
          } else {
            // Abnormal exit
            // Set exit status of foreground process
            dprintf("Child %d exited abnormally due to signal %d\n", childPid, WTERMSIG(childStatus));
            char * conv_str = itos(WEXITSTATUS(childStatus));
            free(e_status->fg);
            e_status->fg = strdup(conv_str);
            free(conv_str);
            
            // Abnormal signals conver to background process
            // Handled later by background process checking loop
            pid_nums->bg_pid = childPid;
            kill(childPid, SIGCONT);
            fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) childPid);
          }

        } else {
          // Background
          // Background checking done at beginning of main loop
          pid_nums->bg_pid = childPid;
          break;

        }

        break;
      }
    

exit:
    // Free memory allocated in loop
    for (int i = 0; i < WORD_LIMIT; i++) {
        free(words[i]);
    }
    free(words);
  }
  // Free allocated memory (outside loop)
  free(line);
  free(home);
  free(pid_nums);
  free(e_status->fg);
  free(e_status->main);
  free(e_status);
  
  exit(EXIT_SUCCESS);

}

//
// Functions
//

int print_prompt(const char *name) {
  char *prompt = NULL;

  if (!getenv(name)) {
    prompt = strdup("");
  } else {
    prompt = strdup(getenv(name));
  }

  int printed = fprintf(stderr, "%s", prompt);
  if (printed == -1) {
    perror("fprintf");
  }

  free(prompt);
  return printed;
}

// Split input based on env variable name
// arr[] should be an array large enough to hold all possible
// input splits
// NULL terminates the array [must have splits + 1]
int split_input(const char *name, char *input, char *arr[]) {
  int scount = 0; // Count of how many times input was split

  // Get delimiters from given environment variable name
  char *delimiters = NULL;
  if (!getenv(name)) {
      delimiters = strdup(" \t\n");
    } else {
      delimiters = strdup(getenv(name));
    }

  // Get first token
  char *token = NULL;
  token = strtok(input, delimiters);

  // Copy current token to given array and
  // Get tokens until whole input is checked
  for (size_t i = 0; i < WORD_LIMIT && token != NULL; i++) {

    char *word = NULL;
    word = strdup(token);

    // Allocate enough memory for word string and copy
    // to given array of strings
    arr[i] = malloc(sizeof arr[i] * (strlen(word) + 1));
    // Attempt malloc again if it failed
    if (!arr[i]) {
      perror("malloc() failed");
      exit(EXIT_FAILURE);
      continue;
     } else {
      strcpy(arr[i], word);
      scount++;
      token = strtok(NULL, delimiters);
     }
    free(word);
  }
  arr[scount] = NULL;
  free(delimiters);
  return scount;
}

// Convert PID to string and return it
// Allocates to heap for a string long enough
// Returns pointer to str, must be freed
char *pidtos(pid_t pid) {
  char *pid_str = NULL;
  pid_str = malloc(sizeof *pid_str * 15);
  if (!pid_str) {
    perror("Malloc() failed");
    exit(EXIT_FAILURE);
  }
  int ret_sprintf = sprintf(pid_str, "%jd",(intmax_t) pid);
  if (ret_sprintf == -1) {
    perror("sprintf() could not convert pid to string");
    exit(EXIT_FAILURE);
   }
  return pid_str;
}

char *itos(int i) {
  char *str = malloc(sizeof str * 15);
  if (!str) {
    perror("malloc() failed");
    exit(EXIT_FAILURE);
  }
  int ret = sprintf( str, "%d", i);
  if (ret < 0) {
    perror("snprintf() failed");
    exit(EXIT_FAILURE);
  }

  return str;

}


int cd_builtin(int argc, char *args[], char *home) {
  dprintf("Executing cd\n");
  char *new_dir = NULL;
  if (argc > 2) {
    fprintf(stderr, "Too many arguments (>1 argument)\n");
    return -1;
  }
  if (argc > 1) {
    new_dir = args[1];
  } else {
     new_dir = home;
  }

  int result = chdir(new_dir);
  if (result == -1) {
    perror("Error");
    return -1;
  }
  dprintf("Successful chdir() call\n");

   return 0;
}

int exit_builtin(int argc, char *args[], char *exit_status) {
  char *status = NULL;

  if (argc > 2) {
    perror("Too many arguments. Only provide one argument\n");
    return -1;
  }
  dprintf("EXIT ARGC: %d", argc);
  if (argc > 1) {
    status = args[1];
  } else {
    status = exit_status;
  }
  
  fprintf(stderr, "\nexit\n");
  exit(atoi(status));
}

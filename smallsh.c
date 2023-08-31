#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <setjmp.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);
bool bg;

char* bg_pid = "";                    // set the default $! variable
char* fg_exit_status = "0";           // set the default $? variable
pid_t new_child; 
pid_t smallsh_pid;
                                   
struct sigaction default_responder = {0};
struct sigaction ignore_responder = {0};
struct sigaction sigint_responder_parent = {0};
struct sigaction sigtstp_responder = {0};
struct sigaction sigint_prev = {0};
struct sigaction sigtstp_prev = {0};

static void sigint_handler(int sig);
static void sigint_handler_2(int sig);

static char param_scan(char const *word, char const **start, char const **end);
static int execute_cmd(char *parsed_words[], int* out_fd);
static int redirection(char *redirector, char *redirect_args[], int* in_fd, int* out_fd);


int main(int argc, char *argv[])
{
  
  FILE *input = stdin;                     // set the input variable to stdin by default
  char *input_fn = "(stdin)";              // use a 2nd var to track the input file's name fn
  if (argc == 2) {                           
    input_fn = argv[1];                    // take input from a file if there is one
    input = fopen(input_fn, "re");          
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many cmd line arguments given");
  }

  char *line = NULL;
  size_t n = 0;                       
 
  default_responder.sa_handler = SIG_DFL;
  ignore_responder.sa_handler = SIG_IGN;
  smallsh_pid = getpid();

  for (;;) {
prompt:;

    // manage background processes
    int status = 0;
    pid_t bg_check = waitpid(0, &status, WNOHANG | WUNTRACED);   
   
    if (bg_check != 0 && bg_check != -1) {

    if (WIFEXITED(status) && bg_check != -1 && bg_check != 0) {
       fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) bg_check, WEXITSTATUS(status));
    }

    else if (WIFSTOPPED(status) && bg_check != -1 && bg_check != 0) {
        fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) bg_check);
        char str_pid[11];
        sprintf(str_pid, "%d", new_child);         // set $! to this new background child 
        bg_pid = str_pid; 
        kill(bg_check, SIGCONT);                   // signal child to continue in background
        
    }  

    else if (WIFSIGNALED(status) && bg_check != -1 && bg_check != 0) {
        fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) bg_check , WTERMSIG(status));  
    }
    goto prompt;
    }             

    if (input == stdin) {          
        char* ps1_prompt = expand("${PS1}");       // print PS1 depending on env variable (can comment out below default prompt prints)
        fprintf(stderr, ps1_prompt);
        uid_t ps1_uid = geteuid();                 // if uid is 0, this is root user (use # prompt)
        if (ps1_uid == 0) { 
          printf("small_shell_# "); 
        } else {
          printf("small_shell_$ ");
        }
      
      // handle signals
      int sigtstp_response = sigaction(SIGTSTP, &ignore_responder, &sigtstp_prev);                    // ignore a ctrl-Z signal
      if (sigtstp_response == -1) { fprintf(stderr, "Ctrl-Z SIGTSTP failed in parent\n"); }
      int sigint_response = sigaction(SIGINT, SIG_DFL, &sigint_prev);                                 // route a ctrl-C signal response to an empty handler func
      if (signal(SIGINT, &sigint_handler_2) == SIG_ERR) { fprintf(stderr, "Ctrl-C SIGINT failed in parent\n"); }
      if (sigint_response == -1) { fprintf(stderr, "Ctrl-C SIGINT failed in parent\n"); }
    }


    ssize_t line_len = getline(&line, &n, input);           // get next line of input

    if (feof(input)) { exit(0); }
    if (line_len < 0) err(1, "line_len is negative: %s \n", input_fn);
  
    if (signal(SIGINT, &sigint_handler) == SIG_ERR) {
      fprintf(stderr, "Ctrl-C SIGINT failed in parent\n"); 
    }
    
    size_t nwords = wordsplit(line);                        // words are parsed and put into words[] global array
    char redirector[1];
    int n_parsed_words = 0;
    bool fill_cmd = true;
    char* parsed_words[MAX_WORDS] = { NULL };
    char* redirect_args[MAX_WORDS] = { NULL };
    int in_fd = fileno(stdin);
    int out_fd = fileno(stdout);
    new_child = getpid();
    int child_exit_status;
    char* expanded_word;


    for (size_t i = 0; i < nwords; ++i) {

      // handle the built-in command "exit"
      if (strcmp(words[0], "exit") == 0) {
          if (nwords > 2) {
            printf("too many arguments given for exit command\n");
            goto prompt;
          }
          if (nwords == 1) {                                  // if no parameter is given then implied parameter is $?
              char* default_exit = expand("$?");
              long default_as_num;
              default_as_num = strtol(default_exit, &default_exit, 10);
              if (*default_exit == '\0') {                    // $? is given as a number, exit with it
                 exit(default_as_num);
              } else {
                 err(1, "the default exit value of $? was not a number\n");
              }
           }
          char* param = words[1];                             // if there're 2 parameters, then exit with the 2nd one
          long param_as_num;
          param_as_num = strtol(param, &param, 10);
          if (*param == '\0') {                               // the parameter is a number, so exit with that exit status
              fg_exit_status = words[1];
              exit(param_as_num);
          } else {                                            // parameter is not a number, so do nothing
              goto prompt;
          }
       }
      
      // handle the built-in command "cd"
      else if (strcmp(words[0], "cd") == 0) { 
          if (nwords > 2) {
            printf("too many arguments given for cd command\n");
            goto prompt;
          }

          if (nwords == 1) {
              char* default_cd = expand("${HOME}");
              chdir(default_cd);
              goto prompt;
          } else {
              char* new_dir = words[1];
              if (strcmp(new_dir, "~") == 0) {
                  new_dir = expand("${HOME}");
              }
              chdir(new_dir);
              goto prompt;
          }
      }

      // handle non-built-in commands
      else {                           
         expanded_word = "pizza";
         char* param;
          
         if (strcmp(words[nwords - 1], "&") == 0) {
            bg = true;
         }

         if (i == 0) {
             new_child = fork();                  // if user cmd isn't 'cd' nor 'exit,' then fork a child
         }

         if (new_child == -1) { 
             fprintf(stderr, "failed to fork new child\n");
             exit(-1);
             return -1;
         } 

         // handle code for the child
         else if (new_child == 0) {          

           // reset signals when input is from stdin
            int sigtstp_response = sigaction(SIGTSTP, &sigtstp_prev, NULL);                          // handle ctrl-Z signal
            if (sigtstp_response == -1) { fprintf(stderr, "Ctrl-Z SIGTSTP failed in child\n"); }
            int sigint_response = sigaction(SIGINT, &sigint_prev, NULL);                             // handle ctrl-C signal
            if (sigint_response == -1) { fprintf(stderr, "Ctrl-C SIGINT failed in child\n"); }
            
                // handle word parsing
                 if (strcmp(words[i], "<") == 0 || strcmp(words[i], ">") == 0 || strcmp(words[i], ">>") == 0) { 
                     strcpy(redirector, words[i]);
                     if (i < nwords - 1 && strcmp(words[i+1], "&") != 0) {
                         if (strcmp(words[i], "$$") == 0 || strcmp(words[i], "$!") == 0 || strcmp(words[i], "$?") == 0 || strncmp(words[i], "${", 2) == 0) {
                             param = expand(strdup(words[i])); 
                             expanded_word = strdup(param);
                         }
                         char* curr_redirect_arg = (char*) malloc(MAX_WORDS);
                         if (strcmp(expanded_word, "pizza") == 0) {
                             strcpy(curr_redirect_arg, words[i+1]);
                         }
                         else {
                             curr_redirect_arg = strdup(expanded_word);
                         }  
                         redirect_args[0] = strdup(curr_redirect_arg);
                         free(curr_redirect_arg);
                     }
                     else { 
                       fprintf(stderr, "Invalid file name supplied to redirection operator"); 
                       exit(-1);
                       goto prompt;
                     }
                     i++;
                     if (redirection(redirector, redirect_args, &in_fd, &out_fd) == -1) { goto prompt; }
                     redirect_args[0] = NULL;                              // reset args for the next operator

                 } else {

                     if (strcmp(words[i], "$$") == 0 || strcmp(words[i], "$!") == 0 || strcmp(words[i], "$?") == 0 || strncmp(words[i], "${", 2) == 0) {
                       param = expand(strdup(words[i])); 
                       expanded_word = strdup(param);
                     }
                
                     if (fill_cmd == true && strcmp(words[i], "&") != 0) {
                         parsed_words[n_parsed_words] = strdup(expand(words[i]));    // scan words here and then expand() only if word's not a command
                         fill_cmd = false;
                         n_parsed_words++;
                         goto end_loop;

                     } else {
                         char* curr_parsed_word = (char*) malloc(MAX_WORDS);
                         if (strcmp(expanded_word, "pizza") == 0 && strcmp(words[i], "&") != 0) {
                             strcpy(curr_parsed_word, expand(words[i]));
                         }
                         else if (strcmp(expanded_word, "pizza") != 0) { 
                             curr_parsed_word = strdup(expand(expanded_word));
                         } 
                         else { goto end_loop; }
                         parsed_words[n_parsed_words] = strdup(curr_parsed_word);
                         n_parsed_words++;
                         free(curr_parsed_word);
                     } 
                 }
end_loop:
            if (i == nwords - 1) {
                 parsed_words[n_parsed_words] = NULL;                              // reset to NULL for execvp() 
                 if (execute_cmd(parsed_words, &out_fd) == -1) { goto prompt; }    // if user cmd is not a valid cmd, do nothing 
            }  
          
	 }

         // handle code for the parent
         else {        
             if (bg == true) {                             // if & was input by user, let child run in background
                 char str_pid[11];
                 sprintf(str_pid, "%d", new_child);        // turn pid into a str
                 bg_pid = str_pid;                         // set the $! var to this bg pid as a str
                 bg = false;
                 goto prompt;
             }

             else {                                        // if no & was input by user, perform a blocking wait on that child 
                 if (i == 0) { 
                 pid_t changed_child = waitpid(new_child, &child_exit_status, WUNTRACED);
                 if (changed_child == -1) { fprintf(stderr, "blocking waitpid call on child failed\n"); }

                 if (WIFEXITED(child_exit_status)) {                                       // if child process exited normally
                     char status_as_str[11];
                     sprintf(status_as_str, "%d", WEXITSTATUS(child_exit_status));         // set $? variable here
                     fg_exit_status = status_as_str;
                 }

                 else if (WIFSTOPPED(child_exit_status)) {                    // if child process was stopped
                     fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) new_child);
                     kill(new_child, SIGCONT);                               // signal child to continue in background
                     char str_pid[11];
                     sprintf(str_pid, "%d", new_child);                      // set $! for this background child
                     bg_pid = str_pid; 
                 }  

                 else if (WIFSIGNALED(child_exit_status)) {                 // if child process was terminated by a signal
                    int new_fg_status = WTERMSIG(child_exit_status) + 128;   
                    char str_fg_status[11];
                    sprintf(str_fg_status, "%d", new_fg_status);            // reset $? as a str 
                    fg_exit_status = str_fg_status;
                 }
               }
             }
           }
      }  

      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
      errno = ENOENT;
      clearerr(input);

    }
  } 
}


char *words[MAX_WORDS] = {0};
/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 *
 * so this takes in a line, and allocates space for each word in that line, and then it
 * stores pointers to those words in that *words array-of-pointers 
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c);           // discard leading space
  for (; *c;) {
    if (wind == MAX_WORDS) break;          // don't read words if we're alrady past the bounds of the words[] array
    if (*c == '#') break;                  // read in a word, remove comments when splitting words
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;                 // handle escape chars
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc failed in wordsplit\n");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char param_scan(char const *word, char const **start, char const **end)
{
  static char const *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = 0;
  *end = 0;
  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        *start = s;
        *end = e + 1;
      }
      break;
    }
  }
  prev = *end;
  return ret;
}


/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {                 // reset new base string, return old one
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }

  size_t n = end ? end - start : strlen(start);             // append [start, end) to the base string
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");                              
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';
  return base;                                       
}


/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */
char *
expand(char const *word)
{
  char const *pos = word;
  char const *start, *end;
  char c = param_scan(pos, &start, &end);             // create pointers to start and end of the word
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {                                         // c will be 0 if it didn't detect a special char:  !$?{
    pid_t pid;
    if (new_child == 0) { 
      pid = getppid(); 
    }
    else { 
       pid = getpid();
    }
    char str_pid[11];
    sprintf(str_pid, "%d", pid);                              // turn pid into a str

    if (c == '!') build_str(bg_pid, NULL);                    // $! is pid of most-recent bg process
    else if (c == '$') build_str(str_pid, NULL);              // $$ is pid of curr smallsh instance
    else if (c == '?') build_str(fg_exit_status, NULL);       // $? is exit status of most recent foreground process
    else if (c == '{') {                                      // ${} is for env variable expansion, 
      char* prev_str = build_str(NULL, NULL);
      char* env_name = build_str(start + 2, end - 1);         
      build_str(NULL, NULL);                                  // clear out the last build_str
      char* env_value = getenv(env_name);                     
      build_str(prev_str, NULL);
      if (env_value == NULL) {
          build_str("", NULL);
      } else {
          build_str(env_value, NULL);
      }
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);      
}


/* handles user-input for non-built-in commands 
* in a child process 
*/
static int execute_cmd(char *parsed_words[], int* out_fd) {
   int new_process = execvp(parsed_words[0], parsed_words);
   if (new_process == -1) { 
      // fprintf(stderr, "failed execvp() in the child\n"); 
      if (errno == EACCES) { fprintf(stderr, "command %s not found\n", parsed_words[0]); }
      exit(-1);
      return -1;
   }
}


/* handles user-input for the redirector operators 
* (<, >, >>) in a child process 
*/
static int redirection(char *redirector, char *redirect_args[], int* in_fd, int* out_fd) {
   if (strcmp(redirector, "<") == 0) {                       // change input to redirect_args[0]
      close(*in_fd);
      *in_fd = open(redirect_args[0], O_RDONLY);
      if (*in_fd == -1) {
          fprintf(stderr, "failed to redirect input via < to %s file\n", redirect_args[0]);
          exit(-1);
          return -1;
      }

   }

   else if (strcmp(redirector, ">") == 0) {                // change output to redirect_args[0] for overwriting
      
      close(*out_fd);
      *out_fd = open(redirect_args[0], O_WRONLY | O_CREAT | O_TRUNC, 0777);
      if (*out_fd == -1) {
          fprintf(stderr, "failed to redirect output via > to %s file\n", redirect_args[0]);
          exit(-1);
          return -1;
      }
   }  

   else if (strcmp(redirector, ">>") == 0) {               // change output to redirect_args[0] for appending
      close(*out_fd);
      *out_fd = open(redirect_args[0], O_WRONLY | O_CREAT | O_APPEND, 0777);
      if (*out_fd == -1) {
          fprintf(stderr, "failed to redirect output via >> to %s file\n", redirect_args[0]);
          exit(-1);
          return -1;
      }
   }   
}


/* empty handler for an empty 
* CTRL-C response 
*/
static void sigint_handler(int sig) {
}


/* generates a non-empty CTRL-C 
* response 
*/
static void sigint_handler_2(int sig) {
    fprintf(stderr, "\n");
    char* ps1_prompt = expand("${PS1}");
    fprintf(stderr, ps1_prompt);
    
}

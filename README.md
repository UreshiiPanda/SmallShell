# SmallShell

#### A small shell written in C

<a name="readme-top"></a>

<!-- smallsh gif -->
![small_shell](https://github.com/UreshiiPanda/SmallShell/assets/39992411/e5b00409-8922-4c6d-b11f-c6a555ac96da)


<!-- ABOUT THE PROJECT -->
## About The Project

This is a small shell program written in C which accepts commands from both a file and from stdin. Small
Shell is modeled off of Bash and performs many of the same functions as Bash does, including:
  - built-in commands "cd" and "exit" handled by the parent process
  - non-built-in commands (eg. echo) are handled in a child process
  - Bash special variables (eg. $!, $$, $?, ${}) expansion
  - handling of redirector operators (eg. >, <, >>)
  - handling processes in the foreground or in the background (when "&" is given)
  - handling or ignoring signals (eg. SIGINT, SIGTSTP) depending on the context
  - management of background processes

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- GETTING STARTED -->
## Getting Started

This program can be run from any command line interpreter after the program has been compiled
by a C compiler (eg. gcc, clang). Note that a makefile has also been provided in order to
handle compilation by executing the "make" command.


### Installation / Execution Steps

1. Clone the repo
   ```sh
      git clone https://github.com/UreshiiPanda/SmallShell.git
   ```
2. Compile
   ```sh
      make
   ```
3. Run Small Shell with or without an input file
   ```sh
      ./smallsh
   ```
   ```sh
      ./smallsh commands_file.txt
   ```


<p align="right">(<a href="#readme-top">back to top</a>)</p>

/**
 * @file forksort.c
 * @author Daniel Fnez (daniel.fenz@tuwien.ac.at)
 * @brief merge sort over multiple processes
 * @date 2021-11-18
 * 
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wait.h>
#include <unistd.h>

/**
 * @brief all relevant informations for one child process
 * child_pid: Process id of child process
 * merge_pipe: pipe information for merging from children, child process can write, parent process can read
 * split_pipe: pipe information for splitting input to children, parent process can write, child process can read
 */
typedef struct
{
  pid_t child_pid;
  int split_pipe[2];
  int merge_pipe[2];
  FILE *stream;
} child_process_info_t;

/**
 * @brief R = Read, W = Write
 * 
 */
enum p_mode_enum
{
  R = 0,
  W = 1
};

char *prog_name;
child_process_info_t c1_info;
child_process_info_t c2_info;

/**
 * @brief closes all open resources
 * @details uses c1_info and c2_info
 */
static void cleanup(void)
{
  // close(c1_info.merge_pipe[0]);
  // close(c1_info.merge_pipe[1]);
  // close(c2_info.merge_pipe[0]);
  // close(c2_info.merge_pipe[1]);
  // close(c1_info.split_pipe[0]);
  // close(c1_info.split_pipe[1]);
  // close(c2_info.split_pipe[0]);
  // close(c2_info.split_pipe[1]);
  // fclose(c1_info.stream);
  // fclose(c2_info.stream);
}

/**
 * @brief prints a message to stderr and exits with EXIT_FAILURE
 * @details uses prog_name to print the program name
 * @param msg error message
 */
static void printErrAndExit(char *msg) {
  fprintf(stderr, "[%s]: Error: %s; Detail: %s\n", prog_name, msg, strerror(errno));
  exit(EXIT_FAILURE);
}

/**
 * @brief checks if a return value is -1 and prints error message if that is the case 
 * 
 * @param retVal return value that is checked
 * @param msg the message that is passed to printErrAndExit
 */
static void tryAndPrintOnErr(int retVal, char *msg) {
  if(retVal == -1) {
    printErrAndExit(msg);
  }
}

/**
 * @brief checks if the pointer provided points to NULL and calls printErrAndExit if that is the case
 * 
 * @param ptr a ptr that is checked with == NULL
 * @param msg the message that is passed to printErrAndExit
 */
static void tryPointerAndPrintOnErr(void *ptr, char *msg) {
  if(ptr == NULL) {
    printErrAndExit(msg);
  }
}

/**
 * @brief open FILE* in c1_info and c2_info in 
 * @details uses c1_info and c2_info to store the file streams and get the fds of the child pipes
 * @param mode either R or W, depending on which pipe should be opened as FILE*
 */
static void openChildStream(int mode)
{
  if(mode == 1) 
  {
    tryPointerAndPrintOnErr(c1_info.stream = fdopen(c1_info.split_pipe[1], "w")
      , "Could not open file stream to write for child 1");
    tryPointerAndPrintOnErr(c2_info.stream = fdopen(c2_info.split_pipe[1], "w")
      , "Could not open file stream to write for child 2");
  }
  else 
  {
    tryPointerAndPrintOnErr(c1_info.stream = fdopen(c1_info.merge_pipe[0], "r")
      , "Could not open file stream to write for child 1");
    tryPointerAndPrintOnErr(c2_info.stream = fdopen(c2_info.merge_pipe[0], "r")
      , "Could not open file stream to write for child 1");
  }
}

/**
 * @brief close streams of the child processes
 * @details uses c1_info and c2_info to get the streams
 */
static void closeChildStream(void)
{
  fclose(c1_info.stream);
  fclose(c2_info.stream);
}

/**
 * @brief open pipes for child, fork and then close unused pipe ends in parent process, 
 * and dup2 the pipe ends to stdin and stdout in the children
 * @details uses c2_info to check if the child that is currently being created is the second child, to close unused pipe ends
 * @param child_info pointer to a struct for the child process infos
 */
static void openChild(child_process_info_t *child_info)
{
  tryAndPrintOnErr(pipe(child_info->merge_pipe), "Could not open pipe");

  tryAndPrintOnErr(pipe(child_info->split_pipe), "Could not open pipe");

  child_info->child_pid = fork();
  if (child_info->child_pid == 0)
  {
    tryAndPrintOnErr(dup2(child_info->split_pipe[R], STDIN_FILENO), "Could not dup2 split pipe read end to stdin");
    tryAndPrintOnErr(dup2(child_info->merge_pipe[W], STDOUT_FILENO), "Could not dup2 merge pipe write end to stdout");
    
    tryAndPrintOnErr(close(child_info->split_pipe[W]), "Could not close W end of split pipe for child");
    tryAndPrintOnErr(close(child_info->split_pipe[R]), "Could not close R end of split pipe for child");

    tryAndPrintOnErr(close(child_info->merge_pipe[R]), "Could not close R end of merge pipe for child");
    tryAndPrintOnErr(close(child_info->merge_pipe[W]), "Could not close W end of merge pipe for child");

    if (child_info == &c2_info)
    {
      tryAndPrintOnErr(close(c1_info.split_pipe[W]), "Could not close W end of split pipe of other child");
      tryAndPrintOnErr(close(c1_info.merge_pipe[R]), "Could not close R end of merge pipe of other child");
    }

    execl(prog_name, prog_name, NULL);
    // shouldn't be reached
  }
  tryAndPrintOnErr(child_info->child_pid, "Could not fork");

  tryAndPrintOnErr(close(child_info->split_pipe[R]), "Could not close split pipe read end in parent process");
  tryAndPrintOnErr(close(child_info->merge_pipe[W]), "Could not close merge pipe write end in parent process");
}

/**
 * @brief split the input of the programm equally to the two child processes
 * @details uses c1_info and c2_info to get the streams to write to the children
 */
static void splitDataToChildren(void)
{
  FILE *childStream;
  int lines;
  char *currLine = NULL;
  size_t currLineBufSize;
  // print line read in main()
  ssize_t eof = getline(&currLine, &currLineBufSize, stdin);
  for (lines = 0; eof != -1; lines++)
  {
    childStream = lines % 2 == 0 ? c1_info.stream : c2_info.stream;
    fprintf(childStream, "%s%s", currLine, currLine[eof - 1] == '\n' ? "" : "\n");
    eof = getline(&currLine, &currLineBufSize, stdin);
  }
}

/**
 * @brief reads a single line from each child, compares them and prints out the "smaller" line and gets the next line from that child
 * @details uses c1_info and c2_info to get the streams to read from the children
 */
static void mergeDataFromChildren(void) {
  size_t lineSizeC1, lineSizeC2;
  char *c1 = NULL;
  char *c2 = NULL;
  ssize_t eofC1 = getline(&c1, &lineSizeC1, c1_info.stream);
  ssize_t eofC2 = getline(&c2, &lineSizeC2, c2_info.stream);
  while (eofC1 != EOF || eofC2 != EOF)
  {
    if (eofC2 == EOF)
    {
      fprintf(stdout, c1);
      eofC1 = getline(&c1, &lineSizeC1, c1_info.stream);
    }
    else if (eofC1 == EOF)
    {
      fprintf(stdout, c2);
      eofC2 = getline(&c2, &lineSizeC2, c2_info.stream);
    } 
    else 
    {
      if (strcmp(c1, c2) < 0)
      {
        fprintf(stdout, c1);
        eofC1 = getline(&c1, &lineSizeC1, c1_info.stream);
      }
      else 
      {
        fprintf(stdout, c2);
        eofC2 = getline(&c2, &lineSizeC2, c2_info.stream);
      }
    }
  }

  free(c1);
  free(c2);
}

/**
 * @brief method to wait for termination of child processes
 * @details uses c1_info and c2_info to get the process ids of the child processes
 */
static void waitForChildren(void)
{
  int status;
  waitpid(c1_info.child_pid, &status, 0);

  if (WEXITSTATUS(status) != EXIT_SUCCESS)
  {
    fprintf(stderr, "Child 1 unsuccessfully terminated");
  }

  waitpid(c2_info.child_pid, &status, 0);

  if (WEXITSTATUS(status) != EXIT_SUCCESS)
  {
    fprintf(stderr, "Child 2 unsuccessfully terminated");
  }
}

int main(int argc, char **argv)
{
  prog_name = argv[0];
  if(atexit(cleanup) != 0) {
    fprintf(stderr, "could not register cleanup function");
  }

  if (argc > 1)
  {
    fprintf(stderr, "Usage: %s\n", prog_name);
  }

  char *firstLine = NULL;
  size_t firstLineBufSize = 0;
  char *secondLine = NULL;
  size_t secondLineBufSize = 0;

  // READ INPUT

  ssize_t c1Length = getline(&firstLine, &firstLineBufSize, stdin);
  ssize_t c2Length = getline(&secondLine, &secondLineBufSize, stdin);

  if (c2Length != EOF) // IF MORE THAN ONE LINE
  {
    // INIT PIPES AND FORK
    openChild(&c1_info);
    openChild(&c2_info);

    // SPLIT INPUT TO CHILDREN
    openChildStream(1);
    fprintf(c1_info.stream, "%s%s", firstLine, firstLine[c1Length - 1] == '\n' ? "" : "\n");
    fprintf(c2_info.stream, "%s%s", secondLine, secondLine[c2Length - 1] == '\n' ? "" : "\n");
    splitDataToChildren();
    closeChildStream();
    // MERGE (WRITE TO STDOUT)
    openChildStream(0);
    mergeDataFromChildren();
    closeChildStream();

    // WAIT FOR CHILDREN
    waitForChildren();
  }
  else // IF ONE LINE
  {
    // WRITE TO STDOUT
    fprintf(stdout, "%s%s", firstLine, firstLine[c1Length - 1] == '\n' ? "" : "\n");
    //fflush(stdout);
  }
  free(firstLine);
  free(secondLine);
  fflush(stdout);

  exit(EXIT_SUCCESS);
}
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "shared.h"

/**
 * @brief prints a message to stderr and errno description to stderr
 * @details uses prog_name to print the program name
 * @param msg error message
 */
void printErr(char *msg)
{
  fprintf(stderr, "[%s]: Error: %s; Detail: %s\n", prog_name, msg, strerror(errno));
}

/**
 * @brief prints a message to stderr and errno description to stderr, exits with EXIT_FAILURE
 * @details uses prog_name to print the program name
 * @param msg error message
 */
void printErrAndExit(char *msg)
{
  printErr(msg);
  exit(EXIT_FAILURE);
}

/**
 * @brief checks if a return value is -1 and prints error message if that is the case 
 * 
 * @param retVal return value that is checked
 * @param msg the message that is passed to printErrAndExit
 * 
 * @return 1 on success, 0 on failure
 */
int tryAndPrintOnErr(int retVal, char *msg)
{
  if (retVal == -1)
  {
    printErr(msg);
    return 0;
  }
  return 1;
}

/**
 * @brief checks if a return value is -1 and prints error message if that is the case 
 * 
 * @param retVal return value that is checked
 * @param msg the message that is passed to printErrAndExit
 */
void tryAndPrintExitOnErr(int retVal, char *msg)
{
  if (retVal == -1)
  {
    printErrAndExit(msg);
  }
}

/**
 * @brief checks if the pointer provided points to NULL and calls printErrAndExit if that is the case
 * 
 * @param ptr a ptr that is checked with == NULL
 * @param msg the message that is passed to printErrAndExit
 * 
 * @return 1 on success, 0 on failure
 */
int tryPointerAndPrintOnErr(void *ptr, char *msg)
{
  if (ptr == NULL)
  {
    printErr(msg);
    return 0;
  }
  return 1;
}

/**
 * @brief checks if the pointer provided points to NULL and calls printErrAndExit if that is the case
 * 
 * @param ptr a ptr that is checked with == NULL
 * @param msg the message that is passed to printErrAndExit
 */
void tryPointerAndExitPrintOnErr(void *ptr, char *msg)
{
  if (ptr == NULL)
  {
    printErrAndExit(msg);
  }
}
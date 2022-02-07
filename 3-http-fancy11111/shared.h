#ifndef SHARED_LIB
#define SHARED_LIB

extern char *prog_name;

void printErrAndExit(char *msg);
void printErr(char *msg);
int tryAndPrintOnErr(int retVal, char *msg);
int tryPointerAndPrintOnErr(void *ptr, char *msg);
void tryAndPrintExitOnErr(int retVal, char *msg);
void tryPointerAndPrintExitOnErr(void *ptr, char *msg);

#endif
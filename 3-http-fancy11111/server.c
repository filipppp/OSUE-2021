#include "shared.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>

#define DEBUG 0
#define debug(format, error, ...) \
  if (DEBUG)                      \
  {                               \
    if (error)                    \
      printf("ERROR: ");          \
    else                          \
      printf("DEBUG: ");          \
    printf(format, __VA_ARGS__);  \
    printf("\n");                 \
  }

/**
 * @brief storing the program name
 * 
 */
char *prog_name;

/**
 * @brief volatile int to handle interrrupts
 * 
 */
volatile sig_atomic_t quit = 0;

/**
 * @brief prints the usage
 * 
 * @details uses prog_name for the usage method
 */
static void usage(void)
{
  printf("%s [-p PORT] [-i INDEX] DOC_ROOT\n", prog_name);
}

/**
 * @brief signal handler
 * 
 * @details sets quit to 1 to handle quit
 */
static void sig_handler(int signal)
{
  quit = 1;
}

/**
 * @brief "main method", opens socket, listens, handles requests, sends requests
 * 
 * @details uses prog_name for error outputs, quit for signal handling
 * 
 * @param port port to start the server on
 * @param docRoot docRoot arguemnt
 * @param index index file argument
 */
static void run(char *port, char *docRoot, char *index)
{
  int sockfd, connfd, addrInfoRes;
  FILE *socketStream, *requestedFile;
  char *filePath = NULL;
  char method[256], requestedPath[256], protocol[256];
  char *line = NULL;
  size_t len = 0;
  ssize_t nread;
  int docRootLen = strlen(docRoot);
  int indexLen = strlen(index);

  struct addrinfo hints, *ai;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((addrInfoRes = getaddrinfo(NULL, port, &hints, &ai)) != 0)
  {
    fprintf(stderr, "[%s]: Could not resolve address info, error code: %s", prog_name, gai_strerror(addrInfoRes));
    exit(EXIT_FAILURE);
  }

  // open socket
  if (!tryAndPrintOnErr((sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)), "Could not open socket"))
  {
    freeaddrinfo(ai);
    exit(EXIT_FAILURE);
  }
  debug("Opened socket, socket fd %d", 0, sockfd);

  int optval = 1;
  tryAndPrintOnErr((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval)), "Could not set socket options");

  // bind
  if (!tryAndPrintOnErr(bind(sockfd, ai->ai_addr, ai->ai_addrlen), "Could not bind socket"))
  {
    freeaddrinfo(ai);
    close(sockfd);
    exit(EXIT_FAILURE);
  }
  debug("Bound socketfd %s", 0, "");

  freeaddrinfo(ai);

  // mark socket as passive
  if (!tryAndPrintOnErr(listen(sockfd, 1), "Could listen on socket"))
  {
    close(sockfd);
    exit(EXIT_FAILURE);
  }
  debug("Started listening on port %s", 0, port);

  while (!quit)
  {
    // wait for request
    debug("Waiting for connection on port %s", 0, port);
    if ((connfd = accept(sockfd, NULL, NULL)) == -1)
    {
      if (errno != EINTR)
      {
        debug("Could not accept connection %s", 1, "");
        close(sockfd);
        exit(EXIT_FAILURE);
      }
      break;
    }
    debug("Opened connection, request socket fd: %d", 0, sockfd);
    // parse request
    socketStream = fdopen(connfd, "r+");
    nread = getline(&line, &len, socketStream);
    debug("Read first line: %s", 0, line);
    if (nread == -1)
    {
      debug("Bad Request, EOF in first line %s", 0, "");
      // send 400 (Bad Request)
      fputs("HTTP/1.1 400 (Bad Request)\r\n", socketStream);
      fputs("Connection: close\r\n", socketStream);
    }
    else if (sscanf(line, "%s %s %s", method, requestedPath, protocol) == EOF)
    {
      debug("Bad Request, did not find expected first line %s", 0, "");
      while ((nread = getline(&line, &len, socketStream)) != -1)
      {
        if (strncmp("\r\n", line, 2) == 0)
        {
          break;
        }
      }
      // send 400 (Bad Request)
      fputs("HTTP/1.1 400 (Bad Request)\r\n", socketStream);
      fputs("Connection: close", socketStream);
    }
    else if (strncmp("GET", method, 4) != 0)
    {
      debug("unsupported method: %s", 0, method);
      while ((nread = getline(&line, &len, socketStream)) != -1)
      {
        if (strncmp("\r\n", line, 2) == 0)
        {
          break;
        }
      }
      // send 501 (Not implemented)
      fputs("HTTP/1.1 501 (Not implemented)\r\n", socketStream);
      fputs("Connection: close", socketStream);
    }
    else if (strcmp("HTTP/1.1", protocol) != 0)
    {
      debug("unsupported protocol: %s", 0, protocol);
      while ((nread = getline(&line, &len, socketStream)) != -1)
      {
        if (strncmp("\r\n", line, 2) == 0)
        {
          break;
        }
      }
      // send 400 (Bad Request)
      fputs("HTTP/1.1 400 (Bad Request)\r\n", socketStream);
      fputs("Connection: close", socketStream);
    }
    else if (strlen(line) != (strlen(requestedPath) + 15))
    {
      debug("unexpected tokens in first line: expected length %zu, got length %zu", 0, strlen(line), strlen(requestedPath) + 15);
      while ((nread = getline(&line, &len, socketStream)) != -1)
      {
        if (strncmp("\r\n", line, 2) == 0)
        {
          break;
        }
      }
      // send 400 (Bad Request)
      fputs("HTTP/1.1 400 (Bad Request)\r\n", socketStream);
      fputs("Connection: close", socketStream);
    }
    else
    {
      // open file
      while ((nread = getline(&line, &len, socketStream)) != -1)
      {
        if (strncmp("\r\n", line, 2) == 0)
        {
          break;
        }
      }

      debug("creating file path for requested file: %s", 0, requestedPath);
      int fileSize = docRootLen;
      int pathLen = strlen(requestedPath);
      fileSize += pathLen;
      int addIndex = requestedPath[pathLen - 1] == '/';
      if (addIndex)
      {
        fileSize += indexLen;
      }

      filePath = calloc(fileSize, sizeof(char));
      if (addIndex)
      {
        sprintf(filePath, "%s%s%s", docRoot, requestedPath + 1, index);
      }
      else
      {
        sprintf(filePath, "%s%s", docRoot, requestedPath + 1);
      }

      debug("trying to open requested file: %s", 0, filePath);
      requestedFile = fopen(filePath, "r");
      if (requestedFile == NULL)
      {
        debug("could not open file %s", 1, filePath);
        // send 404 (Not Found)
        fputs("HTTP/1.1 404 (Not Found)\r\n", socketStream);
        fputs("Connection: close", socketStream);
      }
      else
      {
        // send response

        // calculating the size of the file
        fseek(requestedFile, 0L, SEEK_END);
        long int contentLength = ftell(requestedFile);
        rewind(requestedFile);
        debug("calculated content length: %ld", 0, contentLength);

        // get time
        time_t t;
        struct tm *tmp;

        t = time(NULL);
        tmp = gmtime(&t);
        char timeString[30];
        strftime(timeString, 30, "%a, %d %b %y %T %Z", tmp);
        debug("constructed time string: %s", 0, timeString);

        // send required headers
        fprintf(socketStream, "HTTP/1.1 200 OK\r\nDate: %s\r\nContent-Length: %ld\r\n",
                timeString, contentLength);
        if (strcmp(filePath + fileSize - 6, ".html") == 0 || strcmp(filePath + fileSize - 5, ".htm") == 0)
        {
          fputs("Content-Type: text/html\r\n", socketStream);
        }
        else if (strcmp(filePath + fileSize - 5, ".css") == 0)
        {
          fputs("Content-Type: text/css\r\n", socketStream);
        }
        else if (strcmp(filePath + fileSize - 4, ".js") == 0)
        {
          fputs("Content-Type: application/javascript\r\n", socketStream);
        }

        fputs("Connection: close\r\n\r\n", socketStream);
        debug("sent required headers %s", 0, "");

        char buffer[420];

        while (!feof(requestedFile))
        {
          size_t read = fread(buffer, sizeof(char), 420, requestedFile);
          fwrite(buffer, sizeof(char), read, socketStream);
        }
        debug("sent file %s", 0, "");
        fclose(requestedFile);
      }
      free(filePath);
    }
    fflush(socketStream);
    fclose(socketStream);
  }
  free(line);
  close(sockfd);
}

int main(int argc, char *argv[])
{
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa)); // initialize sa to 0
  sa.sa_handler = sig_handler;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  char *port = "8080";
  int opt_i = 0;
  int opt_p = 0;
  char c;
  char *docRoot;
  char *index = "index.html";
  prog_name = argv[0];
  while ((c = getopt(argc, argv, "p:i:")) != -1)
  {
    switch (c)
    {
    case 'p':
      opt_p++;
      port = optarg;
      break;
    case 'i':
      opt_i++;
      index = optarg;
      break;
    default:
      usage();
      exit(EXIT_FAILURE);
      break;
    }
  }

  if (opt_p > 1 || opt_i > 1)
  {
    usage();
    exit(EXIT_FAILURE);
  }

  if (argc - optind != 1)
  {
    printf("[%s] expected one positional argument, URL, got %d\n", prog_name, argc - optind);
    usage();
    exit(EXIT_FAILURE);
  }
  docRoot = argv[optind];

  debug("Port: %s; Index: %s, Doc Root: %s", 0, port, index, docRoot);
  run(port, docRoot, index);
}
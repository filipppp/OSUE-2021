#include "shared.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define EXIT_PROTOCOL_ERROR 2
#define EXIT_RESPONSE_ERROR 3

#define DEBUG 1
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
 * @brief enum to store the output mode
 * 
 */
typedef enum
{
  S = 0, // write to stdout
  O = 1, // write to file
  D = 2  // write to file in directory
} out_mode_enum;

/**
 * @brief struct to store hostname, path and filename parsed from the request url
 * 
 */
typedef struct
{
  char *host;
  char *path;
  char *file;
} request_info_t;

/**
 * @brief struct to store the arguments provided on call
 * 
 */
typedef struct
{
  out_mode_enum mode;
  char *out;
  char *port;
  char *address;
} prog_param_t;

/**
 * @brief storing the program name
 * 
 */
char *prog_name;

/**
 * @brief prints the usage
 * 
 * @details uses prog_name for the usage method
 */
static void usage()
{
  printf("USAGE: %s [-p PORT] [ -o FILE | -d DIR ] URL\n", prog_name);
}

/**
 * @brief parse the arguments
 * 
 * @param params struct to parse the arguments into
 * @param argc argument count
 * @param argv argument array
 */
static void readArgs(prog_param_t *params, int argc, char *argv[])
{
  debug("readArgs(prog_param_t *params, int argc, char *argv[])%s", 0, "");
  int opt_o = 0;
  int opt_d = 0;
  int opt_p = 0;
  char c;
  char *endptr;

  while ((c = getopt(argc, argv, "p:o:d:")) != -1)
  {
    switch (c)
    {
    case 'd':
      opt_d++;
      params->out = optarg;
      params->mode = D;
      break;
    case 'o':
      opt_o++;
      params->out = optarg;
      params->mode = O;
      break;
    case 'p':
      opt_p++;
      strtol(optarg, &endptr, 10);
      if (*endptr)
      {
        printf("[%s]: Error parsing -p argument\n", prog_name);
        usage();
        exit(EXIT_FAILURE);
      }
      params->port = optarg;
      break;
    default:
      printf("[%s]: Unknown option -%c\n", prog_name, c);
      usage();
      exit(EXIT_FAILURE);
      break;
    }
  }

  if (opt_p > 1)
  {
    printf("[%s] expected -p at most once, got -p %d-times\n", prog_name, opt_p);
    usage();
    exit(EXIT_FAILURE);
  }

  if (opt_o > 1)
  {
    printf("[%s] expected -o at most once, got -o %d-times\n", prog_name, opt_o);
    usage();
    exit(EXIT_FAILURE);
  }

  if (opt_d > 1)
  {
    printf("[%s] expected -d at most once, got -d %d-times\n", prog_name, opt_d);
    usage();
    exit(EXIT_FAILURE);
  }

  if (opt_d == 1 && opt_o == 1)
  {
    printf("[%s] expected either -d [DIR], -o [OUT] or nothing, got both -d and -o\n", prog_name);
    usage();
    exit(EXIT_FAILURE);
  }

  if (argc - optind != 1)
  {
    printf("[%s] expected one positional argument, URL, got %d\n", prog_name, argc - optind);
    usage();
    exit(EXIT_FAILURE);
  }

  params->address = argv[optind];
  if (strncmp("http://", params->address, 7) != 0)
  {
    printf("[%s] expected URL to start with 'http://', got %s\n", prog_name, params->address);
    usage();
    exit(EXIT_FAILURE);
  }
}

/**
 * @brief parses the address provided by the user
 * 
 * @param address the address provided by the user
 * @param request_info the struct where hostname, path and filename are parsed into
 */
static void parseAddress(char *address, request_info_t *request_info)
{
  debug("parseAddress(char *address, request_info_t *request_info)%s", 0, "");
  char *withoutHttp = address + 7;
  char *full = strdup(withoutHttp);

  request_info->host = strsep(&full, ";/?:@=&");
  request_info->path = strchr(withoutHttp, '/');
  if (request_info->path != NULL)
  {
    int fileLen = strlen(request_info->path);
    if (fileLen == 0 || request_info->path[fileLen - 1] == '/')
    {
      request_info->file = "index.html";
    }
    else
    {
      request_info->file = strrchr(request_info->path, '/');
      if (request_info->file == NULL)
      {
      }
      else
      {
        request_info->file = request_info->file + 1;
      }
    }
  }
  else
  {
    request_info->path = "";
    request_info->file = "index.html";
  }
}

/**
 * @brief frees the host pointer in the request info struct
 * 
 * @param request_info 
 */
static void freeRequestInfo(request_info_t request_info)
{
  debug("freeRequestInfo(request_info_t request_info)%s", 0, "");
  free(request_info.host);
}

/**
 * @brief opens the socket file descriptor and connects the socket
 * 
 * @param request_info the request_info_t struct containing the host
 * @param port the port on which the request should be made
 * @return int -1 on error, the sockfd otherwise
 */
static int openSocket(request_info_t request_info, char *port)
{
  struct addrinfo hints, *ai;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  debug("trying to get address info for host: %s, port: %s", 0, request_info.host, port);
  getaddrinfo(request_info.host, port, &hints, &ai);

  int sockfd;

  debug("trying to open socket file descriptor%s", 0, "");
  if (!tryAndPrintOnErr(sockfd = socket(AF_INET, SOCK_STREAM, 0), "Could not open socket"))
  {
    return -1;
  }
  debug("trying to connect socket with fd: %d", 0, sockfd);
  if (!tryAndPrintOnErr(connect(sockfd, ai->ai_addr, ai->ai_addrlen), "Could not connect to socket"))
  {
    close(sockfd);
    return -1;
  }
  debug("freeing address info%s", 0, "");
  freeaddrinfo(ai);
  return sockfd;
}

/**
 * @brief writes the request headers to the socketStream
 * 
 * @param socketStream the socket as a filestream
 * @param request_info the request_info_t struct containing the host and the requested path
 */
static void writeRequest(FILE *socketStream, request_info_t request_info)
{
  // GET <PATH> HTTP/1.1
  // Host: <HOSTNAME>
  // Connection: close
  debug("writing request to filestream; path: %s, host: %s", 0, request_info.path, request_info.host);
  fprintf(socketStream, "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", request_info.path, request_info.host);
  fflush(socketStream);
}

/**
 * @brief reads response from socketStream to either the output file or stdout
 * 
 * @param socketStream the socket as a filestream
 * @param request_info the struct with the hostname, the path and the requested filename
 * @param params the struct with the output mode and the out path
 * @return int 0 on success, 2 on a non 200 response, 3 on protocol error, 1 on other errors
 */
static int readResponse(FILE *socketStream, request_info_t request_info, prog_param_t params)
{
  FILE *out;
  char *line = NULL, *endptr;
  size_t len = 0;
  ssize_t nread;
  int foundContent = 0;
  int lineCount = 0;
  int error = 0; // 0: no error, 1: protocol error, 2: non 200 header

  if (params.mode == S)
  {
    out = stdout;
  }
  else if (params.mode == O)
  {
    if (!tryPointerAndPrintOnErr((out = fopen(params.out, "w+")), "Could not open output file"))
    {
      return 1;
    }
  }
  else
  {
    char *outFilePath;
    int outFileLen = 0;
    int outLen = strlen(params.out);
    int requestedFileLen = strlen(request_info.file);
    outFileLen = outLen + requestedFileLen + 1;
    if (params.out[outLen - 1] != '/')
    {
      outFilePath = calloc(outFileLen + 1, sizeof(char));
      sprintf(outFilePath, "%s/%s", params.out, request_info.file);
    }
    else
    {
      outFilePath = calloc(outFileLen, sizeof(char));
      sprintf(outFilePath, "%s%s", params.out, request_info.file);
    }
    out = fopen(outFilePath, "w+");
    free(outFilePath);
    if (!tryPointerAndPrintOnErr(out, "Could not output file"))
    {
      return -1;
    }
  }

  while (!error && !foundContent && (nread = getline(&line, &len, socketStream)) != -1)
  {
    debug("Response Line %d: %s", 0, lineCount, line);
    if (lineCount == 0)
    {
      char *protocol = strtok(line, " ");
      char *responseCode = strtok(NULL, " ");

      if (strcmp(protocol, "HTTP/1.1") != 0)
      {
        printf("Protocol error!");
        error = EXIT_PROTOCOL_ERROR;
        break;
      }

      long code = strtol(responseCode, &endptr, 10);
      if (*endptr)
      {
        printf("Protocol error!\n");
        error = EXIT_PROTOCOL_ERROR;
        break;
      }
      if (code != 200)
      {
        printf("%s", responseCode);
        char *rest = strtok(NULL, " ");
        while (rest != NULL)
        {
          printf(" %s", rest);
          rest = strtok(NULL, " ");
        }
        printf("\n");
        error = EXIT_RESPONSE_ERROR;
        break;
      }
    }
    else if (strncmp("\r\n", line, 2) == 0)
    {
      foundContent = 1;
    }
    lineCount++;
  }
  if (!error && foundContent)
  {
    char buffer[420];
    while (!feof(socketStream))
    {
      size_t read = fread(buffer, sizeof(char), 420, socketStream);
      fwrite(buffer, sizeof(char), read, out);
    }
    fflush(out);
  }

  free(line);
  if (params.mode != S)
  {
    fclose(out);
  }
  return error;
}

int main(int argc, char *argv[])
{
  prog_param_t params;
  params.port = "80";
  params.out = "";
  params.mode = S;
  request_info_t request_info;
  int sockfd;
  FILE *socketStream = NULL;

  prog_name = argv[0];
  readArgs(&params, argc, argv);
  debug("out: %s, port: %s, address: %s, mode: %d", 0, params.out, params.port, params.address, params.mode);
  // free path and host everywhere afterwards
  parseAddress(params.address, &request_info);
  debug("host: %s, path: %s, file: %s", 0, request_info.host, request_info.path, request_info.file);

  if ((sockfd = openSocket(request_info, params.port)) == -1)
  {
    freeRequestInfo(request_info);
    exit(EXIT_FAILURE);
  }

  debug("trying to open file stream on fd: %d", 0, sockfd);
  if (!tryPointerAndPrintOnErr((socketStream = fdopen(sockfd, "r+")), "Could not open file stream on socket fd"))
  {
    close(sockfd);
    freeRequestInfo(request_info);
    exit(EXIT_FAILURE);
  }

  writeRequest(socketStream, request_info);

  int result = readResponse(socketStream, request_info, params);

  fclose(socketStream);
  freeRequestInfo(request_info);
  exit(result);
}
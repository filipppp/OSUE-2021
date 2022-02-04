/**
* @file client.c
* @author filipppp
* @date 11.01.2021
*
* @brief Can request files over http from a remote or local host.
* @details Binary data is supported.
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/socket.h>
#include <netdb.h>
#include <zlib.h>

/** Buffer size constant  for binary reading and writing */
#define BUFF_SIZE 128
/** Enable gzip encoding */
#define GZIP true

/** Enum for storing output type. */
typedef enum {
    std = 0,
    file = 1,
    directory = 2
} output_e;

/** Global variables to manage application. */
typedef struct {
    char *port;
    output_e output_type;
    char *path;
    char *path_appendix;
    char *hostname;
    char *relative_path;
} options_t;

static char *prog_name;

/**
 * @brief Prints the usage with an extra error message.
 * @details Also terminates the program, so everything should be free'd and closed before calling this method.
 *
 * @param str Error message to be printed.
 */
static void print_usage(char *str) {
    if (str != NULL) {
        fprintf(stderr, "[%s] Error: %s\n", prog_name, str);
    }
    fprintf(stderr, "[%s] Usage: %s [-p PORT] [ -o FILE | -d DIR ] URL\n", prog_name, prog_name);
    exit(EXIT_FAILURE);
}

/**
 * @brief Prints an error to stderr.
 * @details Also terminates the program, so everything should be free'd and closed before calling this method.
 * @param str Error message to be printed.
 */
static void print_error(char *str) {
    fprintf(stderr, "[%s] Error: %s\n", prog_name, str);
    exit(EXIT_FAILURE);
}

/**
 * @brief Handles arguments.
 * @details Everything is handled as stated in the exercise.
 *
 * @param argc This is just argc from main()
 * @param argv This is just argv from main()
 * @param options The options struct where the settings are set in this function.
 */
static void handle_args(int argc, char **argv, options_t *options) {
    /** Exit if too few args */
    prog_name = argv[0];
    if (argc <= 1) {
        print_usage("Too few arguments");
    }

    /** Flags for checking for duplicate pos. arguments */
    bool p_set = false;
    bool output_dir_set = false;
    bool output_file_set = false;

    /** Parse all command line options and arguments */
    int c;
    opterr = 0;
    while ((c = getopt(argc, argv, "p:o:d:")) != -1) {
        switch (c) {
            case 'p':
                if (p_set) print_usage("The positional argument -p is only allowed once.");
                p_set = true;

                char *endptr;
                long val = strtol(optarg, &endptr, 10);
                if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
                    || (errno != 0 && val == 0)) {
                    print_usage("Error converting port to number.");
                }
                if (endptr == optarg)
                    print_usage("The positional argument -p must be followed by an integer. (0-65535)");
                if (val > 65535 || val < 0)
                    print_usage("The positional argument -p must be in the following range: (0-65535)");

                if (*endptr != '\0') {
                    print_usage("The positional argument -p must be an integer the following range: (0-65535)");
                }
                options->port = optarg;
                break;
            case 'o':
                if (output_dir_set) print_usage("The positional arguments -o and -d are mutually exclusive.");
                if (output_file_set) print_usage("The positional argument -o is only allowed once.");

                output_file_set = true;
                options->path = optarg;
                options->output_type = file;
                break;
            case 'd':
                if (output_file_set) print_usage("The positional arguments -o and -d are mutually exclusive.");
                if (output_dir_set) print_usage("The positional argument -d is only allowed once.");

                output_dir_set = true;
                options->path = optarg;
                options->output_type = directory;
                break;
            case '?':
                if (optopt == 'p') print_usage("The positional argument -p must be followed by an integer. (0-65535)");
                if (optopt == 'o') print_usage("The positional argument -o must be followed by a string.");
                if (optopt == 'd') print_usage("The positional argument -o must be followed by a string.");
            default:
                print_usage("Unknown options received.");
        }
    }

    /** Get final URl as parameter */
    options->hostname = argv[optind];
    if (options->hostname == NULL) print_usage("URL missing as argument.");
    if (strncmp(options->hostname, "http://", strlen("http://")) != 0) print_usage("URL has to start with 'http://'.");

    /** Check for correct protocol and extract hostname as well as the relative path */
    options->hostname += strlen("http://");
    char *ptr = strpbrk(options->hostname, ";/?:@=&");
    char *slash_ptr = strpbrk(options->hostname, "/");
    if (ptr != NULL) {
        *ptr = '\0';
        options->relative_path = slash_ptr != NULL ? ++slash_ptr : "";
    } else {
        options->relative_path = "";
    }

    /** Extract correct file name */
    options->path_appendix = "index.html";
    if (strlen(options->relative_path) > 0) {
        for (int i = strlen(options->relative_path) - 1; i >= 0; --i) {
            if (options->relative_path[i] == '/' && i != strlen(options->relative_path) - 1) {
                options->path_appendix = &options->relative_path[i + 1];
                break;
            }
        }
        options->path_appendix = options->relative_path;
    }
}

/**
 * @brief Creates the connection to the server.
 * @details The connection details are used which were parsed in handle_args().
 * @param options Options which should be parsed and filled up by handle_args() beforehand.
 * @return File descriptor for socket or -1 for errors.
 */
static int create_connection(options_t *options) {
    /** Create addrinfo */
    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int res = getaddrinfo(options->hostname, options->port, &hints, &ai);
    if (res != 0) {
        fprintf(stderr, "[%s] Error: getaddrinfo: %s \n", prog_name, gai_strerror(res));
        return -1;
    }

    int sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sockfd < 0) {
        fprintf(stderr, "[%s] Error: couldn't create socket \n", prog_name);
        return -1;
    }

    if (connect(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
        fprintf(stderr, "[%s] Error: couldn't create connection \n", prog_name);
        freeaddrinfo(ai);
        return -1;
    }

    freeaddrinfo(ai);
    return sockfd;
}

/**
 * @brief Checks the response gotten from the local / remote host.
 * @details To be a valid response, the HTTP version must be 1.1 and the HTTP status code must be 200.
 *
 * @param sockfile Socket where the headers should be read from.
 * @return Status of validation.
 */
static int validate_response(FILE *sockfile) {
    /** Get top level header from socket */
    char *buffer = NULL;
    size_t buff_size = 0;
    if (getline(&buffer, &buff_size, sockfile) == -1) {
        fprintf(stderr, "[%s] Error: couldn't get first line of http response \n", prog_name);
        free(buffer);
        return -1;
    }

    /** Check if http version matches */
    if (strncmp(buffer, "HTTP/1.1", strlen("HTTP/1.1")) != 0) {
        fprintf(stderr, "[%s] Protocol error! \n", prog_name);
        free(buffer);
        return -2;
    }

    /** Check if status code is 200 */
    char *buffer_wo_http = buffer + strlen("HTTP/1.1");
    char *endptr = NULL;
    long val = strtol(buffer_wo_http, &endptr, 10);
    if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0) || *endptr != ' ') {
        fprintf(stderr, "[%s] Protocol error! \n", prog_name);
        free(buffer);
        return -2;
    }

    if (val != 200) {
        fprintf(stderr, "[%s] Error: Gotten non 200 status code \n", prog_name);
        free(buffer);
        return -3;
    }

    free(buffer);
    return 0;
}

/**
 * @brief Empties header until only a newline is found.
 * @param sockfile
 */
static void empty_headers(FILE *sockfile) {
    /** Empty out headers and skip to body */
    size_t buff_size = 0;
    char *buff = NULL;
    while (getline(&buff, &buff_size, sockfile) != -1) {
        if (strcmp(buff, "\r\n") == 0) break;
    }
    free(buff);
}

/**
 * @brief Prints response to specified output.
 * @details Uses fread and fwrite, so images and binary stuff also works fine.
 * @param sockfile Socket to be read from.
 * @param output Output to be written to e.g. stdout or a file.
 */
static int write_response(FILE *sockfile, FILE *output) {
    empty_headers(sockfile);
    /** Read content in chunks of 128 bytes and write to output */
    char buffer[BUFF_SIZE];
    size_t read;
    /** Use size as 1 and nmemb as BUFF_SIZE since if used the other way around, you either get BUFF_SIZE bytes or nothing */
    while ((read = fread(buffer, 1, BUFF_SIZE, sockfile)) > 0) {
        fwrite(buffer, 1, read, output);
    }
    return 0;
}


/**
 * @brief Prints response to specified output.
 * @details Uses fread and fwrite, so images and binary stuff also works fine.
 * @param sockfile Socket to be read from.
 * @param output Output to be written to e.g. stdout or a file.
 */
static int write_response_gzip(FILE *sockfile, FILE *output) {
    /** Empty out headers and skip to body */
    empty_headers(sockfile);

    /** Parse gzip */
    int status;
    unsigned int size_inflate;
    /** Create zstream to pass metadata to zlib routines */
    z_stream zs;
    /** In and output buffers for deflate() */
    Bytef in[BUFF_SIZE];
    Bytef out[BUFF_SIZE];

    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    zs.avail_in = 0;
    zs.next_in = Z_NULL;
    /** MAX_WBITS | 16 for Gzip */
    status = inflateInit2(&zs, MAX_WBITS | 16);
    if (status != Z_OK) {
        fprintf(stderr, "[%s] Error: couldn't inflateInit() \n", prog_name);
        return -1;
    }

    /** Outer loops runs until there is no more content to be read */
    do {
        zs.avail_in = fread(in, 1, BUFF_SIZE, sockfile);
        if (ferror(sockfile)) {
            inflateEnd(&zs);
            fprintf(stderr, "[%s] Error: Couldn't read from sockfile \n", prog_name);
            return Z_ERRNO;
        }
        if (zs.avail_in == 0)
            break;
        zs.next_in = in;

        /** Run until all bytes from the BUFF_SIZE big buffer are read */
        do {
            zs.avail_out = BUFF_SIZE;
            zs.next_out = out;
            status = inflate(&zs, Z_NO_FLUSH);
            /** Check of inflate status code */
            switch (status) {
                case Z_NEED_DICT:
                    status = Z_DATA_ERROR;
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    inflateEnd(&zs);
                    fprintf(stderr, "[%s] Error: Couldn't inflate \n", prog_name);
                    return -1;
                default:
                    break;
            }
            /** How much did we inflate? */
            size_inflate = BUFF_SIZE - zs.avail_out;
            if (fwrite(out, 1, size_inflate, output) != size_inflate || ferror(output)) {
                inflateEnd(&zs);
                fprintf(stderr, "[%s] Error: couldn't write to destination \n", prog_name);
                return Z_ERRNO;
            }
        } while (zs.avail_out == 0);
    } while (status != Z_STREAM_END);

    /** Clean up and return */
    inflateEnd(&zs);
    return status == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

/**
* @brief Main entry point
* @details Main function. Options are created and default settings are set.
*
* @param argc
* @param argv
* @return exit code
*/
int main(int argc, char **argv) {
    /** Parse cli args */
    options_t options = {"80", std};
    handle_args(argc, argv, &options);

    /** Setup socket */
    int sockfd = create_connection(&options);
    if (sockfd == -1) {
        exit(EXIT_FAILURE);
    }
    FILE *sockfile = fdopen(sockfd, "r+");
    if (sockfile == NULL) print_error("Error opening socket descriptor.\n");

    /** Send HTTP request */
    fprintf(sockfile, "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n", options.relative_path,
            options.hostname);
    if (GZIP) fprintf(sockfile, "Accept-Encoding: gzip\r\n");
    fprintf(sockfile, "\r\n");
    fflush(sockfile);

    /** Validate response from server */
    int ret = validate_response(sockfile);
    if (ret < 0) {
        fclose(sockfile);
        exit(-ret);
    }

    /** Write response to specified output */
    FILE *f;
    switch (options.output_type) {
        case std:
            f = stdout;
            break;
        case file:
            f = fopen(options.path, "w");
            if (f == NULL) {
                fprintf(stderr, "[%s] Error: Couldn't open file %s \n", prog_name, options.path);
                fclose(sockfile);
                exit(EXIT_FAILURE);
            }
            break;
        case directory: {
            /** If a directory is specified, we have to use the file name as our file where we should write our output to.  */
            char dir_path[strlen(options.path) + strlen(options.path_appendix) + 2];
            strcpy(dir_path, options.path);
            if (options.path[strlen(options.path)] != '/') strcat(dir_path, "/");
            strcat(dir_path, options.path_appendix);

            f = fopen(dir_path, "w");
            if (f == NULL) {
                fprintf(stderr, "[%s] Error: Couldn't open file %s \n", prog_name, dir_path);
                fclose(sockfile);
                exit(EXIT_FAILURE);
            }
            break;
        }
        default:
            f = stdout;
            break;
    }
    GZIP ? write_response_gzip(sockfile, f) : write_response(sockfile, f);

    /** Close everything before exiting */
    if (options.output_type != std) fclose(f);
    fclose(sockfile);
    return EXIT_SUCCESS;
}

/**
* @file server.c
* @author filipppp
* @date 12.01.2021
*
* @brief Acts as a webserver which servers a specific directory to the network.
* @details Is able to server binary files, with appropriate mime-types and compression if needed.
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
#include <signal.h>
#include <time.h>
#include <zlib.h>
#include <assert.h>

/** Buffer size constant  for binary reading and writing */
#define BUFF_SIZE 128

static char *prog_name;

/** HTTP status codes for responses */
typedef enum {
    accepted = 200,
    malformed_req = 400,
    unsupported_method = 501,
    ressource_not_found = 404
} status_e;

/** Stop variable for interrupts */
sig_atomic_t volatile stop = false;

/** Global variables parsed from the CLI */
typedef struct {
    char *port;
    char *default_file;
    char *doc_root;
} options_t;


/** Metadata for a request */
typedef struct {
    FILE *file;
    status_e status;
    char *mime;
    bool gzip;
} response_t;

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
    fprintf(stderr, "[%s] Usage: %s [-p PORT] [ -i INDEX ] DOC_ROOT\n", prog_name, prog_name);
    exit(EXIT_FAILURE);
}

/**
 * @brief Converts enum values to Standart HTTP Codes.
 * @details Only 200, 400, 404, 500 and 501 are implemented. 500 is the default if no match is found.
 * @param status Status enum to be converted.
 * @return String representation according to the Standart HTTP Protocol for the status code passed to the method.
 */
static char *status_to_str(status_e status) {
    switch (status) {
        case accepted:
            return "200 OK";
        case malformed_req:
            return "400 Bad Request";
        case unsupported_method:
            return "501 Not implemented";
        case ressource_not_found:
            return "404 Not Found";
        default:
            return "500 Internal Server Error";
    }
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

    /** Flags for port and index so we can handle errors like double pos. args */
    bool p_set = false;
    bool i_set = false;

    /** Parse all command line options and arguments */
    int c;
    opterr = 0;
    while ((c = getopt(argc, argv, "p:i:")) != -1) {
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
                *endptr = '\0';
                options->port = optarg;
                break;
            case 'i':
                if (i_set) print_usage("The positional argument -i is only allowed once.");

                i_set = true;
                options->default_file = optarg;
                break;
            case '?':
                if (optopt == 'p') print_usage("The positional argument -p must be followed by an integer. (0-65535)");
                if (optopt == 'i') print_usage("The positional argument -i must be followed by a string.");
            default:
                print_usage("Unknown options received.");
        }
    }

    /** Parse DOC_ROOT */
    options->doc_root = argv[optind];
    if (options->doc_root == NULL) print_usage("DOC_ROOT missing as argument.");
    if (options->doc_root[strlen(options->doc_root) - 1] == '/') {
        options->doc_root[strlen(options->doc_root) - 1] = '\0';
    }
}

/**
 * @brief Creates socket but as a server.
 * @details Same as in the client but you have to add bind() and listen()
 * @param options Parsed options from handle_args();
 * @return Status code of the creation process.
 */
static int create_socket(options_t *options) {
    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int res = getaddrinfo(NULL, options->port, &hints, &ai);
    if (res != 0) {
        fprintf(stderr, "[%s] Error: getaddrinfo: %s \n", prog_name, gai_strerror(res));
        return -1;
    }

    int sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sockfd < 0) {
        fprintf(stderr, "[%s] Error: couldn't create socket \n", prog_name);
        return -1;
    }

    /** Server can use old port faster, normally you'd have to wat a minute again to start up the server again */
    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
        fprintf(stderr, "[%s] Error: couldn't bind socket \n", prog_name);
        freeaddrinfo(ai);
        return -1;
    }

    /** N = 5 amount of requests before not being queued anymore */
    if (listen(sockfd, 5) < 0) {
        fprintf(stderr, "[%s] Error: couldn't listen to socket \n", prog_name);
        freeaddrinfo(ai);
        return -1;
    }

    freeaddrinfo(ai);
    return sockfd;
}

/**
 * @brief Gets file size from FILE stream.
 * @param f File of which the size should be determined.
 * @return Size of file stream.
 */
static size_t get_file_size(FILE *f) {
    fseek(f, 0L, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0L, SEEK_SET);
    return sz;
}

/**
 * @brief Sets the MIME-Type for a request.
 * @details Handles only js, css and html files and accordingly only their MIME-Type is set.
 * @param ext File extension parsed out with the dot.
 * @param request Request where the MIME-Type should be set if one is found.
 */
static void set_mime_type(char *ext, response_t *request) {
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
        request->mime = "text/html";
    } else if (strcmp(ext, ".css") == 0) {
        request->mime = "text/css";
    } else if (strcmp(ext, ".js") == 0) {
        request->mime = "application/javascript";
    } else {
        request->mime = NULL;
    }
}

/**
 * @brief Validate request from the client.
 * @details There are three things to be checked. The first line of the request should contain following headers:
 * METHOD REQ_PATH HTTP_VERSION
 *
 * In order to be a valid request, method has to be GET, req_path present and at least an '/' as character and the
 * HTTP_VERSIION has to match the version 1.1.
 *
 * @param conn_file Socket-file from the client.
 * @param options Options parsed by handle_args().
 * @return Request with the correct metadata.
 */
static response_t validate_request(FILE *conn_file, options_t *options) {
    response_t response;
    response.gzip = false;

    /** Read first line and check if request is valid */
    char *buffer = NULL;
    size_t buff_size = 0;
    if (getline(&buffer, &buff_size, conn_file) == -1) {
        fprintf(stderr, "[%s] Error: couldn't get first line of http response \n", prog_name);
        free(buffer);
        response.status = malformed_req;
        return response;
    }

    /** Extract all properties for the first line of the headers */
    char *method = strtok(buffer, " ");
    char *relative_path = strtok(NULL, " ");
    char *http_version = strtok(NULL, " ");
    if (method == NULL || relative_path == NULL || http_version == NULL) {
        fprintf(stderr, "[%s] Error: Request malformed \n", prog_name);
        free(buffer);
        response.status = malformed_req;
        return response;
    }

    /** Check if criteria described above is being met */
    if (strncmp(method, "GET", strlen("GET")) != 0) {
        fprintf(stderr, "[%s] Error: Not a GET response \n", prog_name);
        free(buffer);
        response.status = unsupported_method;
        return response;
    }
    if (strncmp(http_version, "HTTP/1.1", strlen("HTTP/1.1")) != 0) {
        fprintf(stderr, "[%s] Error: Not a valid HTTP version \n", prog_name);
        free(buffer);
        response.status = malformed_req;
        return response;
    }
    if (strlen(relative_path) < 1) {
        fprintf(stderr, "[%s] Error: Not a valid request path \n", prog_name);
        free(buffer);
        response.status = malformed_req;
        return response;
    }

    /** Get path for file to be read */
    char path[strlen(options->doc_root) + strlen(relative_path) + strlen(options->default_file) + 1];
    strcpy(path, options->doc_root);
    strcat(path, relative_path);
    if (relative_path[strlen(relative_path) - 1] == '/') {
        strcat(path, options->default_file);
    }
    /** Extract extension and set MIME-Type if possible */
    char *ext_ptr = strchr(path, '.');
    if (ext_ptr != NULL) {
        set_mime_type(ext_ptr, &response);
    } else {
        response.mime = NULL;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "[%s] Error: couldn't open resource \n", prog_name);
        free(buffer);
        response.status = ressource_not_found;
        return response;
    }

    /** Set metadata and cleanup */
    response.status = accepted;
    response.file = file;
    free(buffer);
    return response;
}


/**
 * @brief Reads from a file and writes it to the output.
 * @param read_file Stream to be read from.
 * @param output Stream to be written to.
 * @return status code
 */
static int read_and_write(FILE *read_file, FILE *output) {
    char buffer[BUFF_SIZE];
    size_t read;
    while ((read = fread(buffer, 1, BUFF_SIZE, read_file)) > 0) {
        fwrite(buffer, 1, read, output);
    }
    return 0;
}

/**
 * @brief Reads from a file and writes it to the output.
 * @details Uses zlib to compress in gzip format. Must be compressed accordingly in the client.
 * @param source Stream to be read from.
 * @param dest Stream to be written to.
 * @return status code
 */
static int read_and_write_compress(FILE *source, FILE *dest) {
    /** Create zstream to pass metadata to zlib routines */
    z_stream zs;
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    /** MAX_WBITS | 16 for Gzip */
    int status = deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS | 16, 8, Z_DEFAULT_STRATEGY);
    if (status != Z_OK) {
        fprintf(stderr, "[%s] Error: couldn't deflateInit2() \n", prog_name);
        return -1;
    }

    Bytef in[BUFF_SIZE];
    Bytef out[BUFF_SIZE];
    unsigned int size_deflate;
    int flush;
    /** Outer loop reads in BUFF_SIZE chunks from the source */
    do {
        /** Amount of ready files (normally BUFF_SIZE if all is read) */
        zs.avail_in = fread(in, 1, BUFF_SIZE, source);
        if (ferror(source)) {
            deflateEnd(&zs);
            fprintf(stderr, "[%s] Error: couldn't read from source properly \n", prog_name);
            return Z_ERRNO;
        }
        /** Set buffer array where avail_in bytes are ready to be read */
        zs.next_in = in;
        /** Check if flush is necessary since its eof */
        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        /** Run deflate until there are no bytes anymore on newly read bytes */
        do {
            /** Set buffer we created before and set the size */
            zs.avail_out = BUFF_SIZE;
            zs.next_out = out;
            /** Deflate to output buffer */
            status = deflate(&zs, flush);
            assert(status != Z_STREAM_ERROR); /** Shouldn't happen */
            /** How many bytes were written? BUFF_SIZE - empty space left */
            size_deflate = BUFF_SIZE - zs.avail_out;
            if (fwrite(out, 1, size_deflate, dest) != size_deflate || ferror(dest)) {
                deflateEnd(&zs);
                fprintf(stderr, "[%s] Error: couldn't write to destination \n", prog_name);
                return Z_ERRNO;
            }
        } while (zs.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&zs);
    return Z_OK;
}

/**
 * @brief Reads and skips all unnecessary data so that we can continue replying to the request
 * @param conn_file File to be skipped through.
 */
static void dump_read_data(FILE *conn_file, response_t *response) {
    size_t buff_size = 0;
    char *buff = NULL;
    while (getline(&buff, &buff_size, conn_file) != -1) {
        if (strncmp(buff, "Accept-Encoding:", strlen("Accept-Encoding:")) == 0) {
            char *token = strtok(buff, " ,;");
            do {
                if (strstr(token, "gzip") != NULL) {
                    response->gzip = true;
                }
            } while ((token = strtok(NULL, " ,;")) != NULL);

        }
        if (strcmp(buff, "\r\n") == 0) break;
    }
    free(buff);
}

/** Signal handler */
void handle_signal() { stop = true; }

/**
* @brief Main entry point
* @details Main function. Options are created and default settings are set.
*
* @param argc
* @param argv
* @return exit code
*/
int main(int argc, char **argv) {
    options_t options = {"8080", "index.html"};
    handle_args(argc, argv, &options);

    int sockfd = create_socket(&options);
    if (sockfd == -1) {
        exit(EXIT_FAILURE);
    }

    /** Handle interrupts */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);

    while (!stop) {
        /** Accept a request and handle it */
        int connfd = accept(sockfd, NULL, NULL);
        if (connfd < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[%s] Error: couldn't accept connection on socket. Exiting.. \n", prog_name);
            continue;
        }

        /** Open socket-file to client and read request from it */
        FILE *conn_file = fdopen(connfd, "r+");
        if (conn_file == NULL) {
            fprintf(stderr, "[%s] Error: couldn't oppen connection file for request. Exiting.. \n", prog_name);
            continue;
        }
        response_t request = validate_request(conn_file, &options);

        /** Create response for request */
        char buff[100];
        time_t tmi;
        struct tm *utcTime;
        time(&tmi);
        utcTime = gmtime(&tmi);
        /** RFC-822 time expression */
        strftime(buff, 100, "%a, %d %b %y %T %Z", utcTime);

        /** Skip through content so we can reply to request */
        dump_read_data(conn_file, &request);
        /** Send response */
        if (request.status == 200) {
            fprintf(conn_file, "HTTP/1.1 %s\r\nDate: %s\r\nContent-Length: %zu\r\nConnection: close\r\n",
                    status_to_str(request.status), buff,
                    get_file_size(request.file));
            if (request.mime != NULL) {
                fprintf(conn_file, "Content-Type: %s\r\n", request.mime);
            }
            if (request.gzip) {
                fprintf(conn_file, "Content-Encoding: gzip\r\n");
            }
            fprintf(conn_file, "\r\n");


            int status = request.gzip ? read_and_write_compress(request.file, conn_file) : read_and_write(request.file,
                                                                                                          conn_file);
            if (status == -1) {
                fprintf(stderr, "[%s] Error: Couldn't write to client. \n", prog_name);
            }
            fflush(conn_file);
            fclose(request.file);
        } else {
            fprintf(conn_file, "HTTP/1.1 %s\r\nDate: %s\r\nConnection: close\r\n\r\n", status_to_str(request.status),
                    buff);
            fflush(conn_file);
        }
        fclose(conn_file);
    }

    /** Cleanup */
    close(sockfd);
    return EXIT_SUCCESS;
}

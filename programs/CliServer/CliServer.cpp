/****************************************************************************
 *
 *   @copyright Copyright (c) 2024 Dave Hylands     <dhylands@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the MIT License version as described in the
 *   LICENSE file in the root of this repository.
 *
 ****************************************************************************/
/**
 *   @file   TestDeviceServer.h
 *
 *   @brief  Implements a Socker based server that then communicates with
 *           bioloid devices, either real or emulated.
 *
 ****************************************************************************/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <termios.h>

#include "DumpMem.h"
#include "LinuxColorLog.h"
#include "Log.h"

static constexpr in_port_t DEFAULT_PORT = 8888;

enum {
    // Options assigned a single character code can use that charater code
    // as a short option.

    OPT_DEBUG = 'd',
    OPT_PORT = 'p',
    OPT_VERBOSE = 'v',
    OPT_HELP = 'h',

    // Options from this point onwards don't have any short option equivalents

    OPT_FIRST_LONG_OPT = 0x80,
};

static const char* g_pgm_name;

struct option g_long_option[] = {
    // clang-format off
    // option       has_arg              flasg      val
    // -----------  ------------------- ----------- ------------
    {"debug",       no_argument,        nullptr,    OPT_DEBUG},
    {"help",        no_argument,        nullptr,    OPT_HELP},
    {"port",        required_argument,  nullptr,    OPT_PORT},
    {"verbose",     no_argument,        nullptr,    OPT_VERBOSE},
    {},
    // clang-format on
};

//! @brief  Verbose flag, set when -v is passed on the command line.
int g_verbose = 0;

//! @brief Debug flag, set when -d is passed on the command line.
int g_debug = 0;

static void usage(void);

//! @brief Main program.
//! @returns 0 if everything was successful
//! @returns non-zero if an error occurs.
int main(
    int argc,    //!< [in] Number of command line arguments.
    char** argv  //!< [in] Array of command line arguments.
) {
    auto log = LinuxColorLog(stdout);

    in_port_t port = DEFAULT_PORT;

    // Figure out which directory our executable came from

    g_pgm_name = basename(argv[0]);

    // Figure out the short options from our options structure
    //
    // The * 2 is to allow for the case that all of the short arguments
    // require an argument (and hence take up 2 characters in the short string)
    //
    // The + 1 is for the trailing null character.

    char short_opts_str[sizeof(g_long_option) / sizeof(g_long_option[0]) * 2 + 1];
    char* short_opts = short_opts_str;
    struct option* scan_opt;
    int opt;

    for (scan_opt = g_long_option; scan_opt->name != NULL; scan_opt++) {
        if ((scan_opt->flag == NULL) && (scan_opt->val < OPT_FIRST_LONG_OPT)) {
            *short_opts++ = (char)scan_opt->val;

            if (scan_opt->has_arg != no_argument) {
                *short_opts++ = ':';
            }
        }
    }
    *short_opts++ = '\0';

    // Parse the command line options

    while ((opt = getopt_long(argc, argv, "dhv", g_long_option, NULL)) > 0) {
        switch (opt) {
            case OPT_DEBUG: {
                g_debug = true;
                break;
            }

            case OPT_PORT: {
                const char* port_str = optarg;
                port = atoi(port_str);
                break;
            }

            case OPT_VERBOSE: {
                g_verbose = true;
                break;
            }
            case '?':
            case OPT_HELP: {
                usage();
                return 1;
            }
        }
    }

    if (g_verbose) {
        Log::debug("g_debug = %d", g_debug);
        Log::debug("port = %d", port);
    }

    int listen_socket;
    if ((listen_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        Log::error("Call to socket failed: '%s'", strerror(errno));
        exit(1);
    }
    int enable = 1;
    if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        Log::error("Failed to set REUSEADDR socket option: %s", strerror(errno));
        exit(1);
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_port = htons(port);
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_family = AF_INET;

    if (bind(listen_socket, (const sockaddr*)&server, sizeof(server)) < 0) {
        Log::error("Failed to bind to port: %d: %s", port, strerror(errno));
        exit(1);
    }

    Log::info("Listening on port %d ...", port);
    if (listen(listen_socket, 1) < 0) {
        Log::error("Failed to listen for incoming connection: %s", strerror(errno));
    }

    struct sockaddr_in client;
    memset(&client, 0, sizeof(client));
    socklen_t client_len = sizeof(client);
    int socket;
    if ((socket = accept(listen_socket, (sockaddr*)&client, &client_len)) < 0) {
        Log::error("Failed to accept incoming connection: %s", strerror(errno));
        exit(1);
    }
    printf("Accepted connection from %s:%d", inet_ntoa(client.sin_addr), ntohs(client.sin_port));

    ssize_t bytesRcvd;
    uint8_t buf[1024];
    while ((bytesRcvd = recv(socket, buf, sizeof(buf), 0)) > 0) {
        DumpMem("R", 0, buf, bytesRcvd);
        // gadget.ProcessBytes(buf, bytesRcvd);
    }

    close(socket);
    close(listen_socket);

    if (g_verbose) {
        Log::debug("Done");
    }

    exit(0);
    return 0;  // Get rid of warning about not returning anything
}  // main

//! @brief Prints program usage.
void usage() {
    Log::info("Usage: %s [option(s)] host port", g_pgm_name);
    Log::info("%s", "");
    Log::info("Connect to a network port");
    Log::info("%s", "");
    Log::info("  -d, --debug       Turn on debug output");
    Log::info("  -h, --help        Display this message");
    Log::info("  -v, --verbose     Turn on verbose messages");
}

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
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <termios.h>

#include "Bus.h"
#include "CorePacketHandler.h"
#include "DumpMem.h"
#include "LinuxColorLog.h"
#include "LinuxSerialBus.h"
#include "Log.h"
#include "SocketBus.h"

enum {
    // Options assigned a single character code can use that charater code
    // as a short option.

    OPT_DEBUG = 'd',
    OPT_PORT = 'p',
    OPT_SERIAL = 's',
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
    {"serial",      required_argument,  nullptr,    OPT_SERIAL},
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

    char const* portStr = SocketBus::DEFAULT_PORT_STR;
    char const* serialPortStr = "";

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
                portStr = optarg;
                break;
            }

            case OPT_SERIAL: {
                serialPortStr = optarg;
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
        Log::debug("portStr = %s", portStr);
    }

    uint8_t cmdPacketData[256];
    uint8_t rspPacketData[256];
    Packet cmdPacket(LEN(cmdPacketData), cmdPacketData);
    Packet rspPacket(LEN(rspPacketData), rspPacketData);
    SocketBus socketBus(&cmdPacket, &rspPacket);
    LinuxSerialBus serialBus(&cmdPacket, &rspPacket);

    socketBus.setDebug(true);
    serialBus.setDebug(true);

    CorePacketHandler corePacketHandler;
    int fd = -1;

    IBus* bus = nullptr;
    if (serialPortStr[0] == '\0') {
        socketBus.add(corePacketHandler);
        if (socketBus.setupServer(portStr) != IBus::Error::NONE) {
            exit(1);
        }
        fd = socketBus.socket();
        bus = &socketBus;
    } else {
        serialBus.add(corePacketHandler);
        printf("Opening serial port\n");
        if (serialBus.open(serialPortStr, 115200) != IBus::Error::NONE) {
            exit(1);
        }
        printf("Serial port opened\n");
        fd = serialBus.serial();
        bus = &serialBus;
    }

    while (true) {
        struct pollfd pfd = {
            .fd = fd,
            .events = POLLIN,
            .revents = 0,
        };
        if (poll(&pfd, 1, -1) < 0) {
            Log::error("Poll failed: %s", strerror(errno));
            break;
        }
        if (pfd.revents == 0) {
            continue;
        }
        if ((pfd.revents & POLLRDHUP) != 0) {
            Log::info("Remote disconnected");
            break;
        }
        if ((pfd.revents & POLLIN) == 0) {
            Log::error("Unexexpected poll revent: 0x%04x", static_cast<unsigned int>(pfd.revents));
            continue;
        }

        if (auto rc = bus->processByte(); rc != Packet::Error::NONE) {
            if (rc != Packet::Error::NOT_DONE) {
                Log::error("Error processing packet: %s", as_str(rc));
            }
            continue;
        }

        // We've parsed a packet.
        bus->handlePacket();
    }

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
    Log::info("  -p, --port PORT   Port to run server on");
    Log::info("  -v, --verbose     Turn on verbose messages");
}

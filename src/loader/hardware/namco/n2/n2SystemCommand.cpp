#include "n2SystemCommand.hpp"

#if defined(_WIN32) || defined(__MINGW32__)

#include "n2Host.h"

#include "../../../log/log.h"
#include "../../../redirections/filesystem.h"

#include <cstring>
#include <filesystem>
#include <fstream>

/* The cabinet shells out for its network configuration, work disk size and
 * console mode.  These are the same for every N2 title; anything one title asks
 * for is handled by that title's own module first. */
extern "C" int n2HandleHostSystemCommand(const char *command)
{
    if (std::strncmp(command, "perl etc/ifconfig.pl > ", 23) == 0)
    {
        std::error_code directoryError;
        std::filesystem::create_directories("tmp", directoryError);

        /*
         * etc/ifconfig.pl prints "$interface @address @mask @mac $link" - the
         * interface number, then IPv4, netmask and MAC as individual decimal
         * bytes, then the ethtool link flag.  Sixteen numbers in all.
         */
        int interfaceIndex = 0;
        int link = 0;
        unsigned char address[4] = {127, 0, 0, 1};
        unsigned char mask[4] = {255, 0, 0, 0};
        unsigned char mac[6] = {0, 0, 0, 0, 0, 0};

        const bool haveAdapter =
            n2HostNetworkInterface(&interfaceIndex, address, mask, mac, &link) != 0;

        std::ofstream output(redirectTempPath(command + 23), std::ios::trunc | std::ios::binary);
        output << interfaceIndex;
        for (unsigned char value : address)
            output << " " << static_cast<int>(value);
        for (unsigned char value : mask)
            output << " " << static_cast<int>(value);
        for (unsigned char value : mac)
            output << " " << static_cast<int>(value);
        output << " " << link << "\n";

        if (haveAdapter)
            log_info("Namco N2: reported host interface %d.%d.%d.%d (MAC %02X:%02X:%02X:%02X:%02X:%02X)",
                     address[0], address[1], address[2], address[3],
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        else
            log_info("Namco N2: no usable host adapter; reported a loopback interface");
        return output ? 0 : 1;
    }

    if (std::strncmp(command, "sudo perl etc/usbsize.pl >", 26) == 0)
    {
        std::error_code directoryError;
        std::filesystem::create_directories("tmp", directoryError);

        /*
         * etc/usbsize.pl prints the second column of "busybox df", i.e. the
         * work disk's total size in 1K blocks.  The loader keeps that storage
         * next to the game, so report the volume it actually lives on.
         */
        const unsigned long long kilobytes = n2HostWorkDiskKilobytes();
        std::ofstream output(redirectTempPath(command + 26), std::ios::trunc | std::ios::binary);
        output << kilobytes << "\n";
        log_info("Namco N2: reported a work disk of %llu 1K blocks", kilobytes);
        return output ? 0 : 1;
    }

    if (std::strncmp(command, "stty ", 5) == 0)
    {
        log_debug("Namco N2: ignoring a console setting (%s)", command);
        return 0;
    }

    if (std::strncmp(command, "sudo ", 5) == 0)
    {
        const char *privileged = command + 5;

        const char *clock = privileged;
        if (std::strncmp(clock, "busybox ", 8) == 0)
            clock += 8;
        if (std::strncmp(clock, "date ", 5) == 0 || std::strncmp(clock, "hwclock", 7) == 0)
        {
            log_debug("Namco N2: ignoring the cabinet's clock update (%s)", command);
            return 0;
        }

        if (std::strncmp(privileged, "ifconfig ", 9) == 0 ||
            std::strncmp(privileged, "route ", 6) == 0)
        {
            return n2HostNetworkCommand(command);
        }

        if (std::strncmp(privileged, "mkdir -p ", 9) == 0)
        {
            std::error_code error;
            std::filesystem::create_directories(redirectTempPath(privileged + 9), error);
            return error ? 1 : 0;
        }

        if (std::strncmp(privileged, "mount ", 6) == 0)
        {
            log_info("Namco N2: no USB medium is emulated; refusing %s", command);
            return 1;
        }
        if (std::strncmp(privileged, "umount ", 7) == 0)
            return 0;
        if (std::strncmp(privileged, "cp ", 3) == 0 || std::strncmp(privileged, "rm ", 3) == 0)
        {
            log_warn("Namco N2: refused a USB transfer with nothing mounted: %s", command);
            return 1;
        }

        log_warn("Namco N2: ignoring an unhandled privileged command: %s", command);
        return 1;
    }
    return -1;
}

#endif

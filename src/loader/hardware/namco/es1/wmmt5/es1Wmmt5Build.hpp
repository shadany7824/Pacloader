#pragma once

#include <cstddef>
#include <cstdint>

/* Where each hooked function sits in one WMN5r build. The three builds share
 * their code but not its layout, and a few object layouts moved with it. */
struct Wmmt5Build
{
    const char *name;

    /* The fifth digit of the dongle serial names the market the dongle was
     * issued for, and the title refuses one from another market with E1911. */
    char regionDigit;

    uintptr_t log[3];

    uintptr_t haspLogin;
    uintptr_t haspLogout;
    uintptr_t haspEncrypt;
    uintptr_t haspDecrypt;
    uintptr_t haspGetSize;
    uintptr_t haspRead;
    uintptr_t haspWrite;

    uintptr_t contentRouter;
    uintptr_t networkState;
    uintptr_t linkCheck;
    uintptr_t peerCheck;
    uintptr_t billingSave;
    uintptr_t decryptToken;
    uintptr_t monitorStep;
    uintptr_t interfaceUpdate;
    uintptr_t offlineExpired;
    /* The two jumps inside the content router check. */
    uintptr_t routerAccept;
    uintptr_t routerReject;
    /* The Mucha hostname in .rodata, or 0 to leave it to the DNS redirects. */
    uintptr_t muchaHost;

    uintptr_t powerOn;
    uintptr_t powerOff;

    uintptr_t banaInit;
    uintptr_t banaAttach;
    uintptr_t banaIsCommandExecuting;
    uintptr_t banaReqLed;
    uintptr_t banaReqAction;
    uintptr_t banaReqBeep;
    uintptr_t banaReqCancel;
    uintptr_t banaReqSendUrlTo;
    uintptr_t banaReqWaitTouch;
    uintptr_t banaReset;

    uintptr_t str400Send;
    uintptr_t str400Receive;

    uintptr_t unitCheck;
    uintptr_t terminalSerialKnown;
    /* Offsets into the unit check's state object. */
    size_t unitStepOffset;
    size_t unitTerminalAnsweredOffset;
};

/* The build the detected title runs, or nullptr outside WMMT5. */
const Wmmt5Build *es1Wmmt5Build();

#pragma once

#include <cstdint>

namespace es1
{
enum class StrPcbState : uint8_t
{
    Closed,
    Idle,
    PoweringOn,
    SelfChecking,
    Running,
};

class StrPcbStateMachine
{
  public:
    void reset(bool reportsUnprompted, bool volunteersSelfCheck);
    void close();

    void powerOn();
    void powerOff();
    void beginSelfCheck();
    void finishSelfCheck();
    void reportPoweredSelfCheck();

    /* Resume the stream after a long period in which the title did not poll
     * the serial device.  Keep power/self-check state, but make the next wheel
     * report immediately available instead of carrying an old cadence over. */
    void resynchronize(uint64_t nowMilliseconds);

    /* Return at most one unsolicited report per board-period.  The caller
     * supplies a monotonic millisecond clock so this class remains independent
     * from the Windows timer implementation. */
    bool nextUnprompted(uint8_t report[3], uint16_t wheelPosition,
                       uint64_t nowMilliseconds);
    StrPcbState state() const;
    const char *stateName() const;

  private:
    bool reportsUnprompted_ = false;
    bool volunteersSelfCheck_ = false;
    bool initialPowerReportPending_ = false;
    uint64_t nextWheelReportMilliseconds_ = 0;
    StrPcbState state_ = StrPcbState::Closed;
};
}

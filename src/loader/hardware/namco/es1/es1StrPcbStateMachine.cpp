#include "es1StrPcbStateMachine.hpp"

#include <cstring>

namespace
{
constexpr uint8_t MotorRunning[3] = {'C', '0', '1'};
constexpr uint8_t WheelReport[1] = {'H'};
/* The STR PCB reports its position continuously.  Four milliseconds matches
 * the cadence used by the real serial bridge without flooding the virtual
 * descriptor when the guest polls it more often. */
constexpr uint64_t WheelReportPeriodMilliseconds = 4;
}

namespace es1
{
void StrPcbStateMachine::reset(bool reportsUnprompted, bool volunteersSelfCheck)
{
    reportsUnprompted_ = reportsUnprompted;
    volunteersSelfCheck_ = volunteersSelfCheck;
    initialPowerReportPending_ = reportsUnprompted && volunteersSelfCheck;
    nextWheelReportMilliseconds_ = 0;
    state_ = StrPcbState::Idle;
}

void StrPcbStateMachine::close()
{
    initialPowerReportPending_ = false;
    nextWheelReportMilliseconds_ = 0;
    state_ = StrPcbState::Closed;
}

void StrPcbStateMachine::powerOn()
{
    initialPowerReportPending_ = false;
    nextWheelReportMilliseconds_ = 0;
    state_ = StrPcbState::PoweringOn;
}

void StrPcbStateMachine::powerOff()
{
    initialPowerReportPending_ = false;
    nextWheelReportMilliseconds_ = 0;
    state_ = StrPcbState::Idle;
}

void StrPcbStateMachine::beginSelfCheck()
{
    initialPowerReportPending_ = false;
    nextWheelReportMilliseconds_ = 0;
    state_ = StrPcbState::SelfChecking;
}

void StrPcbStateMachine::finishSelfCheck()
{
    if (state_ == StrPcbState::SelfChecking)
    {
        state_ = StrPcbState::Running;
        nextWheelReportMilliseconds_ = 0;
    }
}

void StrPcbStateMachine::reportPoweredSelfCheck()
{
    initialPowerReportPending_ = false;
    nextWheelReportMilliseconds_ = 0;
    state_ = StrPcbState::Running;
}

void StrPcbStateMachine::resynchronize(uint64_t nowMilliseconds)
{
    if (state_ == StrPcbState::Running || state_ == StrPcbState::PoweringOn)
        nextWheelReportMilliseconds_ = nowMilliseconds;
}

bool StrPcbStateMachine::nextUnprompted(uint8_t report[3], uint16_t wheelPosition,
                                        uint64_t nowMilliseconds)
{
    if (!report || !reportsUnprompted_ || state_ == StrPcbState::Closed)
        return false;

    if (initialPowerReportPending_)
    {
        std::memcpy(report, MotorRunning, sizeof(MotorRunning));
        initialPowerReportPending_ = false;
        state_ = StrPcbState::PoweringOn;
        nextWheelReportMilliseconds_ = nowMilliseconds + WheelReportPeriodMilliseconds;
        return true;
    }

    if (state_ == StrPcbState::Idle || state_ == StrPcbState::SelfChecking)
        return false;

    if (nextWheelReportMilliseconds_ != 0 &&
        nowMilliseconds < nextWheelReportMilliseconds_)
        return false;

    report[0] = WheelReport[0];
    report[1] = static_cast<uint8_t>(wheelPosition >> 8);
    report[2] = static_cast<uint8_t>(wheelPosition);
    nextWheelReportMilliseconds_ = nowMilliseconds + WheelReportPeriodMilliseconds;
    return true;
}

StrPcbState StrPcbStateMachine::state() const
{
    return state_;
}

const char *StrPcbStateMachine::stateName() const
{
    switch (state_)
    {
        case StrPcbState::Closed:
            return "closed";
        case StrPcbState::Idle:
            return "idle";
        case StrPcbState::PoweringOn:
            return "powering-on";
        case StrPcbState::SelfChecking:
            return "self-checking";
        case StrPcbState::Running:
            return "running";
    }
    return "unknown";
}
}

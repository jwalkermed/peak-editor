#pragma once

#include "peak/core/Sysex.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <deque>

// The single CoreMIDI connection to the Peak, shared by every plugin instance
// and the standalone app in a process (use via juce::SharedResourcePointer).
// Plugins talk to the hardware directly rather than through host MIDI routing.
class PeakConnection final : private juce::MidiInputCallback
{
public:
    struct LogEntry
    {
        double timeMs = 0.0;
        bool outgoing = false;
        peak::Bytes bytes;
    };

    PeakConnection();
    ~PeakConnection() override;

    // Device selection. An empty identifier closes the port.
    void openInput (const juce::String& identifier);
    void openOutput (const juce::String& identifier);
    juce::String getInputIdentifier() const;
    juce::String getOutputIdentifier() const;
    bool isInputOpen() const;
    bool isOutputOpen() const;

    // Picks the saved ports if present, otherwise the first ports named "Peak".
    void autoConnect();

    int getChannel() const noexcept { return channel; } // 0-15
    void setChannel (int newChannel);

    bool send (const juce::MidiMessage& message);
    bool sendSysex (const peak::Bytes& message); // must include F0 ... F7

    // Monitor log and captured incoming SysEx (thread-safe snapshots).
    std::vector<LogEntry> getLog() const;
    std::vector<peak::Bytes> getCapturedSysex() const;
    void clearLog();
    void clearCapturedSysex();
    std::atomic<bool> logRealtime { false };

    // Async notifications on the message thread.
    juce::ChangeBroadcaster logChanged;   // new log entries or captures
    juce::ChangeBroadcaster stateChanged; // ports opened/closed or device list changed

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void addToLog (bool outgoing, const juce::uint8* data, int size);
    void deviceListChanged();

    static constexpr size_t maxLogEntries = 5000;
    static constexpr size_t maxCaptured = 2048;

    juce::ApplicationProperties settings;
    juce::MidiDeviceListConnection deviceListConnection;

    mutable juce::CriticalSection portLock;
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;

    mutable juce::CriticalSection logLock;
    std::deque<LogEntry> log;
    std::vector<peak::Bytes> captured;

    int channel = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakConnection)
};

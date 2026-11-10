#pragma once

#include "peak/CoreMidiTransport.h"
#include "peak/core/ParamMidi.h"
#include "peak/core/Sysex.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <deque>

// The single CoreMIDI connection to the Peak, shared by every plugin instance
// and the standalone app in a process (use via juce::SharedResourcePointer).
// Plugins talk to the hardware directly rather than through host MIDI routing.
class PeakConnection final : private juce::AsyncUpdater,
                             private juce::MidiKeyboardState::Listener
{
public:
    struct LogEntry
    {
        double timeMs = 0.0;
        bool outgoing = false;
        peak::Bytes bytes;
    };

    using PortInfo = CoreMidiTransport::PortInfo;

    PeakConnection();
    ~PeakConnection() override;

    static std::vector<PortInfo> getInputs();
    static std::vector<PortInfo> getOutputs();

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
    bool sendSysex (const peak::Bytes& message, double delayMs = 0.0); // must include F0 ... F7; CoreMIDI schedules delayed sends
    bool sendParam (const peak::ParamDef&, int value);
    // Audio thread: a channel message from the DAW track, moved to the Peak's channel and
    // scheduled delayMs ahead. No allocation and not logged (the monitor would allocate).
    void sendPerformance (const uint8_t* data, size_t size, double delayMs);
    // Sends a long run of CC/NRPN messages spaced out so DIN/network bridges keep up.
    bool sendPaced (const std::vector<peak::ShortMessage>& messages, double msBetween = 2.0);

    // Notes for the Peak. Anything that presses keys here (the on-screen keyboard,
    // a computer keyboard, or the hardware keyboard input) is played on the Peak.
    juce::MidiKeyboardState keyboardState;

    // An extra MIDI input (e.g. a controller keyboard) whose notes, bend, mod wheel,
    // aftertouch and sustain are forwarded to the Peak on its channel.
    void openKeyboardInput (const juce::String& identifier);
    juce::String getKeyboardInputIdentifier() const;

    // Selects a memory slot on the Peak the way it does itself: CC 32 = bank (A = 1), then
    // Program Change = slot (verified on firmware 2.2).
    bool sendPatchSelect (int bank, int slot);

    // Parameter changes, patch dumps and patch selections (bank 0-3, slot) received from
    // the Peak since the last call.
    void takeIncoming (std::vector<peak::ParamChange>& changes, std::vector<peak::Bytes>& dumps,
                       std::vector<std::pair<int, int>>& patchSelections);

    // Monitor log and captured incoming SysEx (thread-safe snapshots).
    std::vector<LogEntry> getLog() const;
    std::vector<peak::Bytes> getCapturedSysex() const;
    void clearLog();
    void clearCapturedSysex();
    std::atomic<bool> logRealtime { false };

    // Async notifications on the message thread.
    juce::ChangeBroadcaster logChanged;   // new log entries or captures
    juce::ChangeBroadcaster stateChanged; // ports opened/closed or device list changed
    juce::ChangeBroadcaster incoming;     // takeIncoming() has something

private:
    void handleIncoming (const uint8_t* data, size_t size); // CoreMIDI thread
    void handleAsyncUpdate() override; // device list changed
    void handleKeyboardInput (const uint8_t* data, size_t size); // CoreMIDI thread
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int note, float velocity) override;
    void addToLog (bool outgoing, const juce::uint8* data, int size);

    static constexpr size_t maxLogEntries = 5000;
    static constexpr size_t maxCaptured = 2048;

    juce::ApplicationProperties settings;

    CoreMidiTransport transport;
    CoreMidiTransport keyboardTransport; // input only

    mutable juce::CriticalSection logLock;
    std::deque<LogEntry> log;
    std::vector<peak::Bytes> captured;

    peak::ParamMidiDecoder decoder; // MIDI thread only
    std::vector<peak::ParamChange> pendingChanges;
    std::vector<peak::Bytes> pendingDumps;
    std::vector<std::pair<int, int>> pendingSelections;
    int lastBankSelect = 1; // CC 32 from the Peak (MIDI thread only)

    std::atomic<int> channel { 0 }; // read on the MIDI thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakConnection)
};

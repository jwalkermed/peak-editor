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
    // The background check for changes made on the Peak that it doesn't announce (e.g. its Initialise):
    // sends a current-sound request without logging it, and keeps its reply out of the log too.
    bool sendQuietRequest (const peak::Bytes&);
    // When the editor last sent SysEx or a controller to the Peak, or received a SysEx dump
    // (Time::getMillisecondCounterHiRes); the background check waits for a quiet moment.
    double getLastActivityMs() const noexcept { return lastActivityMs.load(); }
    // Audio thread: a channel message from the DAW track, moved to the Peak's channel and
    // scheduled delayMs ahead. No allocation and not logged (the monitor would allocate).
    void sendPerformance (const uint8_t* data, size_t size, double delayMs);
    // While muted, notes from the on-screen keyboard, a controller and the DAW track don't
    // reach the Peak (Save: an empty slot has nothing to audition). Muting stops held notes.
    void setNotesMuted (bool);

    // The editor's own preferences (e.g. whether the keyboard strip is shown), kept with the MIDI ports.
    juce::PropertiesFile* getUserSettings() { return settings.getUserSettings(); }

    // MIDI clock from the plugin (DAW tempo): a realtime byte (F8 clock, FA start, FC stop),
    // scheduled `delayMs` ahead. Safe on the audio thread.
    void sendRealtime (uint8_t status, double delayMs);
    // What the plugin is sending, for the Clock status line: running, and the DAW's tempo.
    std::atomic<bool> dawClockRunning { false }, dawClockSent { false };
    std::atomic<double> dawClockBpm { 0.0 };
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
    std::atomic<bool> notesMuted { false };
    std::atomic<double> lastActivityMs { 0.0 };
    std::atomic<bool> quietReplyExpected { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeakConnection)
};

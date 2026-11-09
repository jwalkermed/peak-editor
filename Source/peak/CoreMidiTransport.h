#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Direct CoreMIDI input/output. JUCE 9's device list omits network (RTP-MIDI)
// sessions such as "Network NovPeak", so the editor talks to CoreMIDI itself.
class CoreMidiTransport
{
public:
    struct PortInfo
    {
        std::string name; // display name, e.g. "Network NovPeak"
        std::string id;   // CoreMIDI unique ID as text, stable across launches
    };

    // onReceive gets one complete MIDI message (SysEx reassembled) on a CoreMIDI
    // thread. onDevicesChanged is called on a CoreMIDI notification thread.
    CoreMidiTransport (std::function<void (const uint8_t*, size_t)> onReceive,
                       std::function<void()> onDevicesChanged);
    ~CoreMidiTransport();

    static std::vector<PortInfo> getSources();
    static std::vector<PortInfo> getDestinations();

    bool openInput (const std::string& id); // empty id closes
    bool openOutput (const std::string& id);
    std::string getInputId() const;
    std::string getOutputId() const;

    // Sends one message, optionally scheduled delayMs from now (CoreMIDI does the timing).
    bool send (const uint8_t* data, size_t size, double delayMs = 0.0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

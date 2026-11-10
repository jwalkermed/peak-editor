#include "CoreMidiTransport.h"

#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>

#include <atomic>
#include <mutex>

namespace
{
std::string stringProperty (MIDIObjectRef obj, CFStringRef key)
{
    CFStringRef value = nullptr;
    if (MIDIObjectGetStringProperty (obj, key, &value) != noErr || value == nullptr)
        return {};

    char buffer[512] {};
    CFStringGetCString (value, buffer, sizeof (buffer), kCFStringEncodingUTF8);
    CFRelease (value);
    return buffer;
}

std::string uniqueId (MIDIObjectRef obj)
{
    SInt32 id = 0;
    MIDIObjectGetIntegerProperty (obj, kMIDIPropertyUniqueID, &id);
    return std::to_string (id);
}

bool isOffline (MIDIObjectRef obj)
{
    SInt32 offline = 0;
    MIDIObjectGetIntegerProperty (obj, kMIDIPropertyOffline, &offline);
    return offline != 0;
}

template <typename Count, typename Get>
std::vector<CoreMidiTransport::PortInfo> listEndpoints (Count count, Get get)
{
    std::vector<CoreMidiTransport::PortInfo> ports;
    for (ItemCount i = 0, n = count(); i < n; ++i)
    {
        const auto e = get (i);
        if (e != 0 && ! isOffline (e))
            ports.push_back ({ stringProperty (e, kMIDIPropertyDisplayName), uniqueId (e) });
    }
    return ports;
}

MIDIEndpointRef findEndpoint (bool source, const std::string& id)
{
    const auto n = source ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; ++i)
    {
        const auto e = source ? MIDIGetSource (i) : MIDIGetDestination (i);
        if (e != 0 && uniqueId (e) == id)
            return e;
    }
    return 0;
}

// Converts MIDI 1.0 UMP words into byte-stream messages, reassembling SysEx.
class UmpToBytes
{
public:
    template <typename Emit>
    void feed (const UInt32* words, UInt32 count, Emit&& emit)
    {
        for (UInt32 i = 0; i < count;)
        {
            const auto w = words[i];
            const auto type = w >> 28;

            if (type == 0x1) // system common / real-time
            {
                const uint8_t status = (w >> 16) & 0xFF;
                const uint8_t msg[] { status, uint8_t ((w >> 8) & 0x7F), uint8_t (w & 0x7F) };
                const size_t len = (status == 0xF2) ? 3 : (status == 0xF1 || status == 0xF3) ? 2 : 1;
                emit (msg, len);
                i += 1;
            }
            else if (type == 0x2) // MIDI 1.0 channel voice
            {
                const uint8_t status = (w >> 16) & 0xFF;
                const uint8_t msg[] { status, uint8_t ((w >> 8) & 0x7F), uint8_t (w & 0x7F) };
                const auto kind = status & 0xF0;
                emit (msg, (kind == 0xC0 || kind == 0xD0) ? 2 : 3);
                i += 1;
            }
            else if (type == 0x3 && i + 1 < count) // 7-bit SysEx, 6 bytes per packet
            {
                const auto w2 = words[i + 1];
                const auto status = (w >> 20) & 0xF;
                const auto n = std::min<UInt32> ((w >> 16) & 0xF, 6);
                const uint8_t data[] { uint8_t ((w >> 8) & 0x7F), uint8_t (w & 0x7F), uint8_t ((w2 >> 24) & 0x7F),
                                       uint8_t ((w2 >> 16) & 0x7F), uint8_t ((w2 >> 8) & 0x7F), uint8_t (w2 & 0x7F) };

                if (status == 0 || status == 1) // complete or start
                    sysex.assign (1, 0xF0);

                if (! sysex.empty())
                    sysex.insert (sysex.end(), data, data + n);

                if ((status == 0 || status == 3) && ! sysex.empty()) // complete or end
                {
                    sysex.push_back (0xF7);
                    emit (sysex.data(), sysex.size());
                    sysex.clear();
                }
                i += 2;
            }
            else
            {
                // Other message types are 2-4 words; skip by size.
                static constexpr UInt32 sizes[] { 1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4 };
                i += sizes[type];
            }
        }
    }

private:
    std::vector<uint8_t> sysex;
};
} // namespace

struct CoreMidiTransport::Impl
{
    std::function<void (const uint8_t*, size_t)> onReceive;
    std::function<void()> onDevicesChanged;

    MIDIClientRef client = 0;
    MIDIPortRef inPort = 0, outPort = 0;

    mutable std::mutex lock;
    MIDIEndpointRef source = 0, destination = 0;
    std::string sourceId, destinationId;

    UmpToBytes parser; // only touched on the CoreMIDI receive thread
    std::atomic<bool> alive { true };
};

CoreMidiTransport::CoreMidiTransport (std::function<void (const uint8_t*, size_t)> onReceive, std::function<void()> onDevicesChanged)
    : impl (std::make_unique<Impl>())
{
    impl->onReceive = std::move (onReceive);
    impl->onDevicesChanged = std::move (onDevicesChanged);

    auto* raw = impl.get();

    MIDIClientCreateWithBlock (CFSTR ("Peak Editor"), &impl->client, ^(const MIDINotification* n) {
        if (raw->alive && n != nullptr && n->messageID == kMIDIMsgSetupChanged && raw->onDevicesChanged)
            raw->onDevicesChanged();
    });

    MIDIInputPortCreateWithProtocol (impl->client, CFSTR ("in"), kMIDIProtocol_1_0, &impl->inPort, ^(const MIDIEventList* list, void*) {
        if (! raw->alive)
            return;

        const MIDIEventPacket* packet = &list->packet[0];
        for (UInt32 p = 0; p < list->numPackets; ++p)
        {
            raw->parser.feed (packet->words, packet->wordCount, [raw] (const uint8_t* d, size_t s) { raw->onReceive (d, s); });
            packet = MIDIEventPacketNext (packet);
        }
    });

    MIDIOutputPortCreate (impl->client, CFSTR ("out"), &impl->outPort);
}

CoreMidiTransport::~CoreMidiTransport()
{
    impl->alive = false;
    if (impl->inPort != 0)
        MIDIPortDispose (impl->inPort);
    if (impl->outPort != 0)
        MIDIPortDispose (impl->outPort);
    if (impl->client != 0)
        MIDIClientDispose (impl->client);
}

std::vector<CoreMidiTransport::PortInfo> CoreMidiTransport::getSources()
{
    return listEndpoints ([] { return MIDIGetNumberOfSources(); }, [] (ItemCount i) { return MIDIGetSource (i); });
}

std::vector<CoreMidiTransport::PortInfo> CoreMidiTransport::getDestinations()
{
    return listEndpoints ([] { return MIDIGetNumberOfDestinations(); }, [] (ItemCount i) { return MIDIGetDestination (i); });
}

bool CoreMidiTransport::openInput (const std::string& id)
{
    const std::scoped_lock sl (impl->lock);

    if (impl->source != 0)
        MIDIPortDisconnectSource (impl->inPort, impl->source);

    impl->source = id.empty() ? 0 : findEndpoint (true, id);
    impl->sourceId = impl->source != 0 ? id : std::string();

    return impl->source != 0 && MIDIPortConnectSource (impl->inPort, impl->source, nullptr) == noErr;
}

bool CoreMidiTransport::openOutput (const std::string& id)
{
    const std::scoped_lock sl (impl->lock);
    impl->destination = id.empty() ? 0 : findEndpoint (false, id);
    impl->destinationId = impl->destination != 0 ? id : std::string();
    return impl->destination != 0;
}

std::string CoreMidiTransport::getInputId() const
{
    const std::scoped_lock sl (impl->lock);
    return impl->sourceId;
}

std::string CoreMidiTransport::getOutputId() const
{
    const std::scoped_lock sl (impl->lock);
    return impl->destinationId;
}

namespace
{
MIDITimeStamp hostTimeAfter (double delayMs)
{
    static const auto timebase = [] {
        mach_timebase_info_data_t tb;
        mach_timebase_info (&tb);
        return tb;
    }();
    return delayMs > 0.0 ? mach_absolute_time() + (MIDITimeStamp) (delayMs * 1.0e6 * timebase.denom / timebase.numer) : 0;
}
} // namespace

bool CoreMidiTransport::sendShort (const uint8_t* data, size_t size, double delayMs)
{
    if (size == 0 || size > 3)
        return false;

    Byte buffer[64]; // a packet list with one short packet fits easily
    auto* list = reinterpret_cast<MIDIPacketList*> (buffer);
    auto* packet = MIDIPacketListAdd (list, sizeof (buffer), MIDIPacketListInit (list), hostTimeAfter (delayMs), size, data);

    const std::scoped_lock sl (impl->lock);
    return packet != nullptr && impl->destination != 0 && MIDISend (impl->outPort, impl->destination, list) == noErr;
}

bool CoreMidiTransport::send (const uint8_t* data, size_t size, double delayMs)
{
    const std::scoped_lock sl (impl->lock);

    if (impl->destination == 0 || size == 0)
        return false;

    MIDITimeStamp when = 0; // 0 = now
    if (delayMs > 0.0)
    {
        mach_timebase_info_data_t tb;
        mach_timebase_info (&tb);
        when = mach_absolute_time() + (MIDITimeStamp) (delayMs * 1.0e6 * tb.denom / tb.numer);
    }

    std::vector<Byte> buffer (size + 256);
    auto* list = reinterpret_cast<MIDIPacketList*> (buffer.data());
    auto* packet = MIDIPacketListInit (list);
    packet = MIDIPacketListAdd (list, buffer.size(), packet, when, size, data);

    return packet != nullptr && MIDISend (impl->outPort, impl->destination, list) == noErr;
}

#include "PeakConnection.h"

#include "peak/core/MidiDescribe.h"
#include "peak/core/Patch.h"

namespace
{
constexpr auto inputKey = "midiInput";
constexpr auto outputKey = "midiOutput";
constexpr auto channelKey = "midiChannel";

juce::String findPeakPort (const std::vector<PeakConnection::PortInfo>& ports)
{
    for (const auto& p : ports)
        if (juce::String (p.name).containsIgnoreCase ("Peak"))
            return p.id;
    return {};
}

bool isAvailable (const std::vector<PeakConnection::PortInfo>& ports, const juce::String& id)
{
    return id.isNotEmpty() && std::any_of (ports.begin(), ports.end(), [&] (const auto& p) { return id == juce::String (p.id); });
}
} // namespace

PeakConnection::PeakConnection()
    : transport ([this] (const uint8_t* data, size_t size) { handleIncoming (data, size); },
                 [this] { triggerAsyncUpdate(); })
{
    juce::PropertiesFile::Options options;
    options.applicationName = "settings";
    options.filenameSuffix = ".xml";
    options.folderName = "Peak Editor";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    settings.setStorageParameters (options);

    channel = juce::jlimit (0, 15, settings.getUserSettings()->getIntValue (channelKey, 0));
    autoConnect();
}

PeakConnection::~PeakConnection()
{
    cancelPendingUpdate();
}

std::vector<PeakConnection::PortInfo> PeakConnection::getInputs()  { return CoreMidiTransport::getSources(); }
std::vector<PeakConnection::PortInfo> PeakConnection::getOutputs() { return CoreMidiTransport::getDestinations(); }

void PeakConnection::openInput (const juce::String& identifier)
{
    const auto ok = transport.openInput (identifier.toStdString());

    if (identifier.isEmpty() || ok)
        settings.getUserSettings()->setValue (inputKey, identifier);

    stateChanged.sendChangeMessage();
}

void PeakConnection::openOutput (const juce::String& identifier)
{
    const auto ok = transport.openOutput (identifier.toStdString());

    if (identifier.isEmpty() || ok)
        settings.getUserSettings()->setValue (outputKey, identifier);

    stateChanged.sendChangeMessage();
}

juce::String PeakConnection::getInputIdentifier() const  { return transport.getInputId(); }
juce::String PeakConnection::getOutputIdentifier() const { return transport.getOutputId(); }

bool PeakConnection::isInputOpen() const  { return getInputIdentifier().isNotEmpty(); }
bool PeakConnection::isOutputOpen() const { return getOutputIdentifier().isNotEmpty(); }

void PeakConnection::autoConnect()
{
    auto* props = settings.getUserSettings();
    const auto inputs = getInputs();
    const auto outputs = getOutputs();

    if (! isInputOpen())
    {
        const auto saved = props->getValue (inputKey);
        const auto id = isAvailable (inputs, saved) ? saved : findPeakPort (inputs);
        if (id.isNotEmpty())
            openInput (id);
    }

    if (! isOutputOpen())
    {
        const auto saved = props->getValue (outputKey);
        const auto id = isAvailable (outputs, saved) ? saved : findPeakPort (outputs);
        if (id.isNotEmpty())
            openOutput (id);
    }
}

void PeakConnection::setChannel (int newChannel)
{
    channel = juce::jlimit (0, 15, newChannel);
    settings.getUserSettings()->setValue (channelKey, channel.load());
    stateChanged.sendChangeMessage();
}

void PeakConnection::handleAsyncUpdate()
{
    // Device list changed: drop ports whose device went away, then reconnect if the Peak reappeared.
    if (const auto id = getInputIdentifier(); id.isNotEmpty() && ! isAvailable (getInputs(), id))
        transport.openInput ({});

    if (const auto id = getOutputIdentifier(); id.isNotEmpty() && ! isAvailable (getOutputs(), id))
        transport.openOutput ({});

    autoConnect();
    stateChanged.sendChangeMessage();
}

bool PeakConnection::send (const juce::MidiMessage& message)
{
    if (! transport.send (message.getRawData(), (size_t) message.getRawDataSize()))
        return false;

    addToLog (true, message.getRawData(), message.getRawDataSize());
    return true;
}

bool PeakConnection::sendSysex (const peak::Bytes& message)
{
    if (message.size() < 2 || message.front() != 0xF0 || message.back() != 0xF7)
        return false;

    return send (juce::MidiMessage (message.data(), (int) message.size()));
}

bool PeakConnection::sendParam (const peak::ParamDef& def, int value)
{
    bool ok = true;
    for (const auto& m : peak::encodeParam (def, value, channel))
        ok = send (juce::MidiMessage (m[0], m[1], m[2])) && ok;
    return ok;
}

bool PeakConnection::sendPaced (const std::vector<peak::ShortMessage>& messages, double msBetween)
{
    // CoreMIDI schedules each message by timestamp, so this returns immediately.
    for (size_t i = 0; i < messages.size(); ++i)
        if (! transport.send (messages[i].data(), messages[i].size(), 5.0 + (double) i * msBetween))
            return false;

    return true; // paced messages are not added to the monitor log
}

void PeakConnection::takeIncoming (std::vector<peak::ParamChange>& changes, std::vector<peak::Bytes>& dumps)
{
    const juce::ScopedLock sl (logLock);
    changes.swap (pendingChanges);
    dumps.swap (pendingDumps);
    pendingChanges.clear();
    pendingDumps.clear();
}

void PeakConnection::handleIncoming (const uint8_t* data, size_t size)
{
    addToLog (false, data, (int) size);

    bool notify = false;

    if (size == 3)
    {
        if (const auto change = decoder.feed (channel.load(), data[0], data[1], data[2]))
        {
            const juce::ScopedLock sl (logLock);
            pendingChanges.push_back (*change);
            notify = true;
        }
    }
    else if (peak::Patch::looksLikePatchDump (data, size))
    {
        const juce::ScopedLock sl (logLock);
        pendingDumps.emplace_back (data, data + size);
        notify = true;
    }

    if (notify)
        incoming.sendChangeMessage();
}

void PeakConnection::addToLog (bool outgoing, const juce::uint8* data, int size)
{
    if (size <= 0 || (! logRealtime && peak::isRealtime (data, (size_t) size)))
        return;

    LogEntry entry { juce::Time::getMillisecondCounterHiRes(), outgoing, peak::Bytes (data, data + size) };

    {
        const juce::ScopedLock sl (logLock);

        if (! outgoing && data[0] == 0xF0 && captured.size() < maxCaptured)
            captured.push_back (entry.bytes);

        log.push_back (std::move (entry));
        while (log.size() > maxLogEntries)
            log.pop_front();
    }

    logChanged.sendChangeMessage();
}

std::vector<PeakConnection::LogEntry> PeakConnection::getLog() const
{
    const juce::ScopedLock sl (logLock);
    return { log.begin(), log.end() };
}

std::vector<peak::Bytes> PeakConnection::getCapturedSysex() const
{
    const juce::ScopedLock sl (logLock);
    return captured;
}

void PeakConnection::clearLog()
{
    {
        const juce::ScopedLock sl (logLock);
        log.clear();
    }
    logChanged.sendChangeMessage();
}

void PeakConnection::clearCapturedSysex()
{
    {
        const juce::ScopedLock sl (logLock);
        captured.clear();
    }
    logChanged.sendChangeMessage();
}

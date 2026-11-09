#include "PeakConnection.h"

#include "peak/core/MidiDescribe.h"

namespace
{
constexpr auto inputKey = "midiInput";
constexpr auto outputKey = "midiOutput";
constexpr auto channelKey = "midiChannel";

juce::String findPeakPort (const juce::Array<juce::MidiDeviceInfo>& devices)
{
    for (const auto& d : devices)
        if (d.name.containsIgnoreCase ("Peak"))
            return d.identifier;
    return {};
}

bool isAvailable (const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& id)
{
    return id.isNotEmpty() && std::any_of (devices.begin(), devices.end(), [&] (const auto& d) { return d.identifier == id; });
}
} // namespace

PeakConnection::PeakConnection()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "settings";
    options.filenameSuffix = ".xml";
    options.folderName = "Peak Editor";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    settings.setStorageParameters (options);

    channel = juce::jlimit (0, 15, settings.getUserSettings()->getIntValue (channelKey, 0));

    deviceListConnection = juce::MidiDeviceListConnection::make ([this] { deviceListChanged(); });
    autoConnect();
}

PeakConnection::~PeakConnection()
{
    const juce::ScopedLock sl (portLock);
    if (input != nullptr)
        input->stop();
}

void PeakConnection::openInput (const juce::String& identifier)
{
    std::unique_ptr<juce::MidiInput> newInput;

    if (identifier.isNotEmpty())
        if ((newInput = juce::MidiInput::openDevice (identifier, this)) != nullptr)
            newInput->start();

    {
        const juce::ScopedLock sl (portLock);
        if (input != nullptr)
            input->stop();
        std::swap (input, newInput);
    }

    if (identifier.isEmpty() || input != nullptr)
        settings.getUserSettings()->setValue (inputKey, identifier);

    stateChanged.sendChangeMessage();
}

void PeakConnection::openOutput (const juce::String& identifier)
{
    auto newOutput = identifier.isNotEmpty() ? juce::MidiOutput::openDevice (identifier) : nullptr;

    {
        const juce::ScopedLock sl (portLock);
        std::swap (output, newOutput);
    }

    if (identifier.isEmpty() || output != nullptr)
        settings.getUserSettings()->setValue (outputKey, identifier);

    stateChanged.sendChangeMessage();
}

juce::String PeakConnection::getInputIdentifier() const
{
    const juce::ScopedLock sl (portLock);
    return input != nullptr ? input->getIdentifier() : juce::String();
}

juce::String PeakConnection::getOutputIdentifier() const
{
    const juce::ScopedLock sl (portLock);
    return output != nullptr ? output->getIdentifier() : juce::String();
}

bool PeakConnection::isInputOpen() const  { return getInputIdentifier().isNotEmpty(); }
bool PeakConnection::isOutputOpen() const { return getOutputIdentifier().isNotEmpty(); }

void PeakConnection::autoConnect()
{
    auto* props = settings.getUserSettings();
    const auto inputs = juce::MidiInput::getAvailableDevices();
    const auto outputs = juce::MidiOutput::getAvailableDevices();

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
    settings.getUserSettings()->setValue (channelKey, channel);
    stateChanged.sendChangeMessage();
}

void PeakConnection::deviceListChanged()
{
    // Drop ports whose device went away, then reconnect if the Peak reappeared.
    if (const auto id = getInputIdentifier(); id.isNotEmpty() && ! isAvailable (juce::MidiInput::getAvailableDevices(), id))
    {
        const juce::ScopedLock sl (portLock);
        if (input != nullptr)
            input->stop();
        input.reset();
    }

    if (const auto id = getOutputIdentifier(); id.isNotEmpty() && ! isAvailable (juce::MidiOutput::getAvailableDevices(), id))
    {
        const juce::ScopedLock sl (portLock);
        output.reset();
    }

    autoConnect();
    stateChanged.sendChangeMessage();
}

bool PeakConnection::send (const juce::MidiMessage& message)
{
    {
        const juce::ScopedLock sl (portLock);
        if (output == nullptr)
            return false;
        output->sendMessageNow (message);
    }

    addToLog (true, message.getRawData(), message.getRawDataSize());
    return true;
}

bool PeakConnection::sendSysex (const peak::Bytes& message)
{
    if (message.size() < 2 || message.front() != 0xF0 || message.back() != 0xF7)
        return false;

    return send (juce::MidiMessage (message.data(), (int) message.size()));
}

void PeakConnection::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    addToLog (false, message.getRawData(), message.getRawDataSize());
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

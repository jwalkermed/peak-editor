#include "PeakConnection.h"

#include "peak/core/Wavetable.h"

#include "peak/core/MidiDescribe.h"
#include "peak/core/Patch.h"
#include "peak/core/Settings.h"

namespace
{
constexpr auto inputKey = "midiInput";
constexpr auto outputKey = "midiOutput";
constexpr auto channelKey = "midiChannel";
constexpr auto keyboardKey = "keyboardInput";

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
                 [this] { triggerAsyncUpdate(); }),
      keyboardTransport ([this] (const uint8_t* data, size_t size) { handleKeyboardInput (data, size); },
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
    keyboardState.addListener (this);
    autoConnect();
}

PeakConnection::~PeakConnection()
{
    keyboardState.removeListener (this);
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

    if (getKeyboardInputIdentifier().isEmpty())
        if (const auto saved = props->getValue (keyboardKey); isAvailable (inputs, saved))
            openKeyboardInput (saved);
}

void PeakConnection::openKeyboardInput (const juce::String& identifier)
{
    keyboardState.allNotesOff (0);
    const auto ok = keyboardTransport.openInput (identifier.toStdString());

    if (identifier.isEmpty() || ok)
        settings.getUserSettings()->setValue (keyboardKey, identifier);

    stateChanged.sendChangeMessage();
}

juce::String PeakConnection::getKeyboardInputIdentifier() const
{
    return keyboardTransport.getInputId();
}

void PeakConnection::handleKeyboardInput (const uint8_t* data, size_t size)
{
    if (size < 2 || data[0] < 0x80 || data[0] >= 0xF0)
        return;

    const auto kind = data[0] & 0xF0;
    const auto d1 = data[1], d2 = size > 2 ? data[2] : uint8_t (0);

    // Notes go through the keyboard state so the on-screen keys light up too.
    if (kind == 0x90 && d2 > 0)
        keyboardState.noteOn (1, d1, (float) d2 / 127.0f);
    else if (kind == 0x80 || kind == 0x90)
        keyboardState.noteOff (1, d1, (float) d2 / 127.0f);
    else if (kind == 0xE0 || kind == 0xD0 || kind == 0xA0
             || (kind == 0xB0 && (d1 == 1 || d1 == 2 || d1 == 4 || d1 == 11 || d1 == 64 || d1 == 66 || d1 == 123)))
    {
        // Performance data only: other CCs from a controller could silently change patch parameters.
        uint8_t msg[3] { uint8_t (kind | channel.load()), d1, d2 };
        send (juce::MidiMessage (msg, kind == 0xD0 ? 2 : 3));
    }
}

void PeakConnection::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
{
    if (notesMuted)
        return;
    send (juce::MidiMessage::noteOn (channel.load() + 1, note, velocity));
}

void PeakConnection::handleNoteOff (juce::MidiKeyboardState*, int, int note, float velocity)
{
    send (juce::MidiMessage::noteOff (channel.load() + 1, note, velocity));
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

    if (const auto id = getKeyboardInputIdentifier(); id.isNotEmpty() && ! isAvailable (getInputs(), id))
    {
        keyboardTransport.openInput ({});
        keyboardState.allNotesOff (0);
    }

    autoConnect();
    stateChanged.sendChangeMessage();
}

bool PeakConnection::send (const juce::MidiMessage& message)
{
    if (! transport.send (message.getRawData(), (size_t) message.getRawDataSize()))
        return false;

    if (const auto status = message.getRawData()[0]; status == 0xF0 || (status & 0xF0) == 0xB0 || (status & 0xF0) == 0xC0)
        lastActivityMs = juce::Time::getMillisecondCounterHiRes();
    addToLog (true, message.getRawData(), message.getRawDataSize());
    return true;
}

bool PeakConnection::sendSysex (const peak::Bytes& message, double delayMs)
{
    if (message.size() < 2 || message.front() != 0xF0 || message.back() != 0xF7)
        return false;

    if (delayMs <= 0.0)
        return send (juce::MidiMessage (message.data(), (int) message.size()));

    if (! transport.send (message.data(), message.size(), delayMs))
        return false;
    lastActivityMs = juce::Time::getMillisecondCounterHiRes() + delayMs;
    addToLog (true, message.data(), (int) message.size());
    return true;
}

bool PeakConnection::sendQuietRequest (const peak::Bytes& message)
{
    if (message.size() < 2 || message.front() != 0xF0 || message.back() != 0xF7 || ! transport.send (message.data(), message.size()))
        return false;
    quietReplyExpected = true;
    return true;
}

void PeakConnection::sendRealtime (uint8_t status, double delayMs)
{
    if (status >= 0xF8)
        transport.sendShort (&status, 1, delayMs);
}

void PeakConnection::setNotesMuted (bool muted)
{
    if (muted && ! notesMuted.exchange (true))
        send (juce::MidiMessage::allNotesOff (channel.load() + 1)); // CC 123 (verified safe on 2.2)
    notesMuted = muted;
}

void PeakConnection::sendPerformance (const uint8_t* data, size_t size, double delayMs)
{
    if (size == 0 || size > 3 || data[0] < 0x80 || data[0] >= 0xF0)
        return;
    if (notesMuted && ((data[0] & 0xF0) == 0x90 && size > 2 && data[2] > 0))
        return; // note-ons only: offs still go, so nothing hangs
    uint8_t msg[3] { uint8_t ((data[0] & 0xF0) | channel.load()), size > 1 ? data[1] : uint8_t (0), size > 2 ? data[2] : uint8_t (0) };
    transport.sendShort (msg, size, delayMs);
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

bool PeakConnection::sendPatchSelect (int bank, int slot)
{
    const auto ch = channel.load() + 1;
    return send (juce::MidiMessage::controllerEvent (ch, 32, juce::jlimit (0, 3, bank) + 1))
        && send (juce::MidiMessage::programChange (ch, juce::jlimit (0, 127, slot)));
}

void PeakConnection::takeIncoming (std::vector<peak::ParamChange>& changes, std::vector<peak::Bytes>& dumps,
                                   std::vector<std::pair<int, int>>& patchSelections)
{
    const juce::ScopedLock sl (logLock);
    changes.swap (pendingChanges);
    dumps.swap (pendingDumps);
    patchSelections.swap (pendingSelections);
    pendingChanges.clear();
    pendingDumps.clear();
    pendingSelections.clear();
}

void PeakConnection::handleIncoming (const uint8_t* data, size_t size)
{
    const bool isDump = size > 0 && data[0] == 0xF0;
    const bool quietReply = isDump && peak::Patch::looksLikePatchDump (data, size) && quietReplyExpected.exchange (false);
    if (! quietReply) // the background check's reply isn't logged, and isn't activity
    {
        addToLog (false, data, (int) size);
        if ((isDump && size > 100) || (size >= 2 && ((data[0] & 0xF0) == 0xB0 || (data[0] & 0xF0) == 0xC0)))
            lastActivityMs = juce::Time::getMillisecondCounterHiRes(); // a dump, or a knob or patch change on the Peak
    }

    bool notify = false;
    const auto onOurChannel = size >= 2 && (data[0] & 0x0F) == channel.load();

    // Patch selection on the Peak: CC 32 (bank, A = 1) then Program Change (slot).
    if (onOurChannel && (data[0] & 0xF0) == 0xB0 && data[1] == 32 && size == 3)
        lastBankSelect = data[2];
    else if (onOurChannel && (data[0] & 0xF0) == 0xC0)
    {
        if (lastBankSelect >= 1 && lastBankSelect <= 4)
        {
            const juce::ScopedLock sl (logLock);
            pendingSelections.emplace_back (lastBankSelect - 1, data[1]);
            notify = true;
        }
    }
    else if (size == 3)
    {
        if (const auto change = decoder.feed (channel.load(), data[0], data[1], data[2]))
        {
            const juce::ScopedLock sl (logLock);
            pendingChanges.push_back (*change);
            notify = true;
        }
    }
    else if (peak::Patch::looksLikePatchDump (data, size) || peak::SettingsDump::isSettingsDump (data, size) || peak::isWavetableName (data, size)
             || peak::isWavetableData (data, size))
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

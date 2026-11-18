#include <M5Unified.h>
#include <WebSocketsClient.h>
#include <esp_wifi.h>
#include <esp_partition.h>
#include <LittleFS.h>
#include <time.h>
#include <cmath>
#include "watch_art.h"
#include "local_pairing.h"
#include "ui_locale.h"
#include "provision.h"
#include "motion.h"
#include "playback.h"
#include "notice_chime.h"

extern const uint8_t annaMicroHeiBitmap[];
extern const uint8_t annaSmoothBitmap[];
#if defined(ANNA_LANG_JA)
extern const uint8_t annaJapaneseBitmap[];
#endif

namespace {
constexpr int width = 466, center = 233;
constexpr uint32_t sampleRate = 16000;
constexpr size_t samples = AnnaPlayback::packetSamples;
constexpr size_t captureSamples = 2 * samples;
constexpr uint32_t navy = 0x050b21, white = 0xffffff, muted = 0xb4c2d9;
constexpr uint32_t blue = 0x9dbbdf, yellow = 0xf0c099, apricot = 0xe6aa85;
constexpr uint32_t slate = 0x394967, replyBackground = 0x14223a;
constexpr int voiceButtonW = 210, voiceButtonH = 68;
constexpr int voiceButtonX = center - voiceButtonW / 2, voiceButtonY = center - voiceButtonH / 2;
constexpr int replyTop = 151;
constexpr unsigned historyLimit = 10;
constexpr uint32_t screenIdleMs = 30000;
constexpr uint32_t wifiAttemptMs = 12000;
#if defined(ANNA_LANG_JA)
lgfx::PointerWrapper uiFontData(annaJapaneseBitmap);
#else
lgfx::PointerWrapper uiFontData(annaSmoothBitmap);
#endif
lgfx::VLWfont smoothFont;
const lgfx::U8g2font bitmapFont(annaMicroHeiBitmap);
const lgfx::IFont* uiFont = &bitmapFont;
enum class State { Idle, Record, Waiting, Reply, Dialing, Listening, Speaking, Error };
State state = State::Idle;
WebSocketsClient socket;
M5Canvas frame(&M5.Display), body(&M5.Display);
M5Canvas dial(&frame), wifiOn(&frame), wifiOff(&frame), batteryIcon(&frame), bead(&frame);
bool gatewayReady = false, enabled = false, released = false, inCall = false, captureCapped = false;
bool resumeListening = false, finishReply = false;
bool preparingReply = false, replyDismissed = false, transcriptOpen = false;
bool awaitingSubmit = false, alwaysOn = false;
bool newMessage = false, noticeRinging = false;
uint32_t lastNoticePoll = 0;
uint32_t noticeRingAt = 0, noticeTextAt = 0, noticeVoiceAt = 0, noticesDone = 0;
bool noticeWokeScreen = false, noticeChimeQueued = false;
String noticeId;
bool recording = false, playing = false;
bool replyHapticSent = false, replyHapticActive = false;
constexpr uint32_t replyHapticMs = 85;
uint32_t replyHapticAt = 0, replyHapticCount = 0;
bool speechGainSet = false;
uint8_t speechDacGain = 0;
int16_t mic[2][captureSamples];
AnnaPlayback playback;
size_t recordHead = 0, recordCount = 0;
bool buffering = true;
uint32_t playbackDrainAt = 0, playbackGaps = 0;
uint32_t lastFrame = 0, since = 0, callSince = 0, pressedBoth = 0;
uint32_t wifiAttemptAt = 0, wifiSwitches = 0;
constexpr uint32_t meshWeakMs = 4000, meshScanIntervalMs = 15000;
constexpr uint32_t meshCooldownMs = 45000, meshConnectMs = 8000;
constexpr int meshWeakRssi = -65, meshRssiDrop = 20, meshMinGain = 10, meshMinCandidateRssi = -67;
int meshPeakRssi = -127;
uint32_t meshLastSample = 0, meshLastScan = 0, meshWeakSince = 0, meshLastRoam = 0;
uint32_t meshScans = 0, meshRoams = 0, meshFallbacks = 0;
bool meshScanning = false, meshJoining = false, resumeMeshCall = false;
constexpr int callMeshWeakRssi = -69, callMeshMinGain = 6, callMeshCandidateRssi = -76;
uint32_t lastCallRecoveryAt = 0;
constexpr uint8_t callScanChannels[] = {1, 6, 11, 8, 2, 3, 4, 5, 7, 9, 10, 12, 13};
size_t callScanIndex = 0;
uint8_t meshScanChannel = 0;
bool callWifiAwake = false;
wifi_ps_type_t idleWifiSleep = WIFI_PS_MIN_MODEM;
constexpr uint32_t callMeshWeakMs = 900, callMeshCooldownMs = 12000;
uint8_t meshTarget[6]{};
String homeSsid, hotspotSsid;
bool tryingHotspot = false;
bool retryCurrentWifi = false;
bool preferHotspot = false;
uint32_t lastScreenActivity = 0;
bool screenAsleep = false, wakePress = false;
uint32_t presentedFrames = 0, panelSyncs = 0;
uint32_t lastMicQueue = 0, micFrames = 0;
enum class IdleReaction : uint8_t { Cuddle, Tickle, Hop };
IdleReaction idleReaction = IdleReaction::Cuddle;
uint32_t idleReactionAt = 0, idleTaps = 0;
float idleFaceX = center, idleFaceY = 240.f, idleFaceRadius = 90.f;
constexpr uint32_t idleReactionMs = 1250;
bool idleTouchCaptured = false, idleRubbing = false;
int idleFingerX = 0, idleFingerY = 0;
float idleRubTilt = 0.f;
uint32_t idleRubCount = 0;
float idleGrabX = 0.f, idleGrabY = 0.f, idleDragX = 0.f, idleDragY = 0.f;
float idleVelocityX = 0.f, idleVelocityY = 0.f, idleFlightDistance = 0.f;
uint32_t idleFingerAt = 0, idleThrows = 0;
enum class IdleTravel : uint8_t { None, Return, Throw };
IdleTravel idleTravel = IdleTravel::None;
uint32_t idleTravelAt = 0;
int micPeak = 0;
AnnaMotion motion;
uint32_t lastMotionAt = 0, motionSamples = 0;
int scroll = 0, touchY = -1;
enum class ReplyDrag : uint8_t { None, Vertical, Horizontal };
ReplyDrag replyDrag = ReplyDrag::None;
struct SavedReply { String text; bool notice = false; };
SavedReply history[historyLimit];
unsigned historyOffset = 0;
uint32_t historyNewest = 0;
bool historyDisk = false, replySaved = false;
int drawnReplyScroll = -1;
bool settingsOpen = false;
bool settingsDismissTouch = false;
uint32_t settingsDismisses = 0;
bool wifiSwitchPending = false;
int gestureY = -1, activeSlider = 0;
constexpr int settingsTrackX = 96, settingsTrackW = 274;
constexpr int volumeSliderY = 182, brightnessSliderY = 233;
constexpr int settingsCardY = 269, settingsCardH = 61, settingsCardW = 157;
constexpr int wifiButtonX = 70, screenModeX = 239;
constexpr int settingsBackX = 121, settingsBackY = 353, settingsBackW = 224, settingsBackH = 45;
uint8_t volume = 128, brightness = 95;
String reply, error;
int level = 0;

enum class LinkEvent : uint8_t { WifiDown = 1, MeshChanged, GatewayDown, GatewayReady, MeshScan, MeshScanFailed };
struct LinkDiagnostic {
    uint32_t sequence, at;
    LinkEvent event;
    int16_t rssi;
    uint8_t reason, channel;
};
constexpr unsigned linkDiagnosticCapacity = 16;
LinkDiagnostic linkDiagnostics[linkDiagnosticCapacity]{};
portMUX_TYPE linkDiagnosticLock = portMUX_INITIALIZER_UNLOCKED;
unsigned linkDiagnosticHead = 0, linkDiagnosticCount = 0;
uint32_t linkDiagnosticSequence = 0, linkDiagnosticBoot = 0, linkDiagnosticSentAt = 0;
int16_t linkRssi = 0;
uint8_t linkChannel = 0, lastHomeNode[6]{};
bool haveHomeNode = false;
uint32_t linkSampleAt = 0;

void recordLinkDiagnostic(LinkEvent event, uint8_t reason = 0, int candidateRssi = 0, uint8_t candidateChannel = 0) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&linkDiagnosticLock);
    if (linkDiagnosticCount == linkDiagnosticCapacity) {
        linkDiagnosticHead = (linkDiagnosticHead + 1) % linkDiagnosticCapacity;
        --linkDiagnosticCount;
    }
    const unsigned index = (linkDiagnosticHead + linkDiagnosticCount++) % linkDiagnosticCapacity;
    const bool scan = event == LinkEvent::MeshScan || event == LinkEvent::MeshScanFailed;
    linkDiagnostics[index] = {++linkDiagnosticSequence, now, event,
                              int16_t(scan ? candidateRssi : linkRssi), reason,
                              scan ? candidateChannel : linkChannel};
    portEXIT_CRITICAL(&linkDiagnosticLock);
}

void acknowledgeLinkDiagnostic(uint32_t sequence) {
    portENTER_CRITICAL(&linkDiagnosticLock);
    if (linkDiagnosticCount && linkDiagnostics[linkDiagnosticHead].sequence == sequence) {
        linkDiagnosticHead = (linkDiagnosticHead + 1) % linkDiagnosticCapacity;
        --linkDiagnosticCount;
    }
    portEXIT_CRITICAL(&linkDiagnosticLock);
    linkDiagnosticSentAt = 0;
}

void sampleLinkDiagnostic(uint32_t now) {
    if (now - linkSampleAt < 500) return;
    linkSampleAt = now;
    const int rssi = WiFi.RSSI();
    if (rssi >= 0) return;
    const uint8_t channel = WiFi.channel();
    portENTER_CRITICAL(&linkDiagnosticLock);
    linkRssi = rssi;
    linkChannel = channel;
    portEXIT_CRITICAL(&linkDiagnosticLock);
    if (tryingHotspot || WiFi.SSID() != homeSsid) return;
    const uint8_t* node = WiFi.BSSID();
    if (!node) return;
    if (haveHomeNode && memcmp(node, lastHomeNode, sizeof(lastHomeNode)) != 0)
        recordLinkDiagnostic(LinkEvent::MeshChanged);
    memcpy(lastHomeNode, node, sizeof(lastHomeNode));
    haveHomeNode = true;
}

void sendLinkDiagnostic(uint32_t now) {
    if (!gatewayReady || (linkDiagnosticSentAt && now - linkDiagnosticSentAt < 2000)) return;
    LinkDiagnostic entry{};
    portENTER_CRITICAL(&linkDiagnosticLock);
    if (linkDiagnosticCount) entry = linkDiagnostics[linkDiagnosticHead];
    portEXIT_CRITICAL(&linkDiagnosticLock);
    if (!entry.sequence) return;
    char data[104];
    snprintf(data, sizeof(data), "wifi-diag:%lu,%lu,%u,%u,%d,%u,%lu",
             static_cast<unsigned long>(linkDiagnosticBoot), static_cast<unsigned long>(entry.sequence),
             unsigned(entry.event), unsigned(entry.reason), entry.rssi, unsigned(entry.channel),
             static_cast<unsigned long>(now - entry.at));
    linkDiagnosticSentAt = now;
    socket.sendTXT(data);
}

void show(State next) { state = next; since = millis(); }

bool isHotspot(const String& ssid) {
    return hotspotSsid.length() && ssid == hotspotSsid;
}

void connectSavedWifi(bool useHotspot, int channel = 0, const uint8_t* bssid = nullptr) {
    tryingHotspot = useHotspot;
    const String& ssid = useHotspot ? hotspotSsid : homeSsid;
    WiFi.begin(ssid.c_str(), Provision::storage.getString(useHotspot ? "hotspot-pass" : "pass").c_str(),
               channel, bssid);
    wifiAttemptAt = millis();
}

void cancelMeshScan() {
    if (!meshScanning) return;
    esp_wifi_scan_stop();
    meshScanning = false;
    WiFi.scanDelete();
}

void switchSavedWifi(bool useHotspot) {
    cancelMeshScan();
    meshJoining = false;
    meshWeakSince = 0;
    socket.disconnect();
    enabled = gatewayReady = false;
    retryCurrentWifi = false;
    WiFi.disconnect(false, false);
    wifiSwitchPending = true;
    ++wifiSwitches;
    connectSavedWifi(useHotspot);
}

void resetAudio();
void start(bool call, bool recovering = false);

void beginCallRecovery() {
    resetAudio();
    resumeMeshCall = inCall = true;
    lastCallRecoveryAt = millis();
    show(State::Dialing);
}

bool tickMeshRoam(uint32_t now, bool inputActive) {
    const bool idle = state == State::Idle || state == State::Reply || state == State::Error;
    const bool activeCall = inCall && (state == State::Listening || state == State::Speaking);
    if ((!idle && !activeCall) || inputActive || wifiSwitchPending || meshJoining ||
        tryingHotspot) {
        meshWeakSince = 0;
        cancelMeshScan();
        return false;
    }

    int count = 0;
    if (meshScanning) {
        count = WiFi.scanComplete();
        if (count == WIFI_SCAN_RUNNING) return false;
    } else {
        if (now - meshLastSample < (activeCall ? 350 : 1000)) return false;
        meshLastSample = now;
    }
    const int currentRssi = WiFi.RSSI();
    if (WiFi.SSID() == homeSsid && currentRssi < 0)
        meshPeakRssi = std::max(meshPeakRssi, currentRssi);
    const bool signalFaded = meshPeakRssi - currentRssi >= meshRssiDrop;
    const bool weak = activeCall ? currentRssi <= callMeshWeakRssi :
                                  currentRssi <= meshWeakRssi || signalFaded;
    if (WiFi.SSID() != homeSsid || currentRssi >= 0 || !weak) {
        meshWeakSince = 0;
        cancelMeshScan();
        return false;
    }
    if (!meshWeakSince) meshWeakSince = now ? now : 1;
    if (!meshScanning) {
        const uint32_t weakMs = activeCall ? callMeshWeakMs : meshWeakMs;
        const uint32_t interval = activeCall ? 420 : meshScanIntervalMs;
        const uint32_t cooldown = activeCall ? callMeshCooldownMs : meshCooldownMs;
        if (now - meshWeakSince < weakMs ||
            (meshLastScan && now - meshLastScan < interval) ||
            (meshLastRoam && now - meshLastRoam < cooldown)) return false;
        meshLastScan = now;
        meshScanChannel = 0;
        if (activeCall) {
            const int currentChannel = WiFi.channel();
            do {
                meshScanChannel = callScanChannels[callScanIndex];
                callScanIndex = (callScanIndex + 1) % sizeof(callScanChannels);
            } while (meshScanChannel == currentChannel);
        }
        // One channel at a time during a call keeps off-channel dwell short.
        // Compare SSIDs in the results so broadcast Mesh beacons can be discovered.
        const int result = WiFi.scanNetworks(true, false, false, activeCall ? 110 : 180,
                                             meshScanChannel);
        meshScanning = result == WIFI_SCAN_RUNNING;
        if (meshScanning) ++meshScans;
        else recordLinkDiagnostic(LinkEvent::MeshScanFailed, uint8_t(-result), 0, meshScanChannel);
        return false;
    }

    meshScanning = false;
    if (count < 0) {
        recordLinkDiagnostic(LinkEvent::MeshScanFailed, uint8_t(-count), 0, meshScanChannel);
        WiFi.scanDelete();
        return false;
    }
    uint8_t current[6];
    const uint8_t* associated = WiFi.BSSID();
    if (!associated) { WiFi.scanDelete(); return false; }
    memcpy(current, associated, sizeof(current));
    const int minGain = activeCall ? callMeshMinGain : meshMinGain;
    int candidateRssi = std::max(activeCall ? callMeshCandidateRssi : meshMinCandidateRssi,
                                 currentRssi + minGain) - 1;
    int candidateChannel = 0, strongestAlternate = -127;
    uint8_t alternateCount = 0, strongestChannel = 0;
    for (int i = 0; i < count && i < 256; ++i) {
        String ssid;
        uint8_t auth, *bssid;
        int32_t rssi, channel;
        if (!WiFi.getNetworkInfo(i, ssid, auth, rssi, bssid, channel) || !bssid ||
            ssid != homeSsid || memcmp(bssid, current, sizeof(current)) == 0) continue;
        if (alternateCount < 255) ++alternateCount;
        if (rssi > strongestAlternate) {
            strongestAlternate = rssi;
            strongestChannel = channel;
        }
        if (rssi > candidateRssi) {
            candidateRssi = rssi;
            candidateChannel = channel;
            memcpy(meshTarget, bssid, sizeof(meshTarget));
        }
    }
    WiFi.scanDelete();
    if (activeCall && (alternateCount || callScanIndex == 0))
        recordLinkDiagnostic(LinkEvent::MeshScan, alternateCount,
                             strongestChannel ? strongestAlternate : 0,
                             strongestChannel ? strongestChannel : meshScanChannel);
    if (!candidateChannel) return false;

    Serial.printf("ANNA_MESH roaming from_rssi=%d to_rssi=%d channel=%d\n",
                  currentRssi, candidateRssi, candidateChannel);
    if (activeCall) beginCallRecovery();
    socket.disconnect();
    enabled = gatewayReady = false;
    WiFi.disconnect(false, false);
    meshJoining = true;
    meshLastRoam = now;
    meshWeakSince = 0;
    connectSavedWifi(false, candidateChannel, meshTarget);
    return true;
}

bool canSwitchWifi() {
    return homeSsid.length() && hotspotSsid.length() &&
           (state == State::Idle || state == State::Reply || state == State::Error);
}

void historyPath(char (&path)[24], uint32_t sequence) {
    snprintf(path, sizeof(path), "/anna-reply-%u", unsigned(sequence % historyLimit));
}

bool historyPartitionIsBlank() {
    const auto* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                      ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "anna-history");
    if (!partition) return false;
    uint8_t bytes[512];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(bytes)) {
        const size_t length = std::min(sizeof(bytes), partition->size - offset);
        if (esp_partition_read(partition, offset, bytes, length) != ESP_OK ||
            !std::all_of(bytes, bytes + length, [](uint8_t value) { return value == 0xff; }))
            return false;
    }
    return true;
}

void loadReplyHistory() {
    constexpr char label[] = "anna-history";
    historyDisk = LittleFS.begin(false, "/anna-history", 3, label) ||
                  (historyPartitionIsBlank() && LittleFS.begin(true, "/anna-history", 3, label));
    if (!historyDisk) return;
    for (unsigned slot = 0; slot < historyLimit; ++slot) {
        char path[24];
        historyPath(path, slot);
        auto file = LittleFS.open(path, "r");
        uint32_t sequence = 0;
        if (!file || file.read(reinterpret_cast<uint8_t*>(&sequence), sizeof(sequence)) != sizeof(sequence) ||
            !sequence || sequence % historyLimit != slot) continue;
        const int notice = file.read();
        const int length = file.available();
        if ((notice != 0 && notice != 1) || length <= 0 || length > 24000) continue;
        String body;
        if (!body.reserve(length)) continue;
        char chunk[256];
        while (body.length() < unsigned(length)) {
            const size_t count = file.read(reinterpret_cast<uint8_t*>(chunk), sizeof(chunk));
            if (!count || !body.concat(chunk, count)) break;
        }
        if (body.length() == unsigned(length)) {
            history[slot] = {std::move(body), notice != 0};
            historyNewest = std::max(historyNewest, sequence);
        }
    }
}

void saveCurrentReply() {
    if (replySaved || reply.isEmpty()) return;
    const uint32_t sequence = ++historyNewest;
    history[sequence % historyLimit] = {reply, newMessage};
    replySaved = true;
    if (!historyDisk) return;

    auto file = LittleFS.open("/anna-reply.tmp", "w");
    if (!file) return;
    const bool written = file.write(reinterpret_cast<const uint8_t*>(&sequence), sizeof(sequence)) ==
                         sizeof(sequence) && file.write(uint8_t(newMessage)) == 1 &&
                         file.write(reinterpret_cast<const uint8_t*>(reply.c_str()), reply.length()) == reply.length();
    file.close();
    if (written) {
        char path[24];
        historyPath(path, sequence);
        LittleFS.rename("/anna-reply.tmp", path);
    }
}

unsigned previousReplyCount() {
    return std::min<unsigned>(historyNewest - (replySaved ? 1 : 0), historyLimit - 1);
}

void turnReplyPage(int direction) {
    const int next = int(historyOffset) + direction;
    if (next < 0 || unsigned(next) > previousReplyCount()) return;
    historyOffset = unsigned(next);
    scroll = 0;
    lastFrame = 0;
}

bool replyVisible() {
    return !inCall && !replyDismissed && reply.length() &&
           (state == State::Waiting || state == State::Speaking || state == State::Reply);
}

bool largeVoiceView() {
    return alwaysOn && !newMessage && !replyDismissed && !transcriptOpen &&
           (inCall || state == State::Record || state == State::Waiting ||
            state == State::Speaking || (state == State::Reply && reply.length()));
}

void endMic() {
    if (recording) { M5.Mic.end(); recording = false; }
    recordHead = recordCount = 0;
}

void endPlayback() {
    if (playing) { M5.Speaker.stop(); M5.Speaker.end(); playing = false; }
    speechGainSet = false;
    playback.clear();
    buffering = true;
    playbackDrainAt = 0;
}

void resetAudio() {
    resumeMeshCall = false;
    endMic(); endPlayback();
    if (replyHapticActive) M5.Power.setVibration(0);
    replyHapticActive = replyHapticSent = false;
    inCall = resumeListening = finishReply = released = captureCapped = preparingReply = false;
    replyDismissed = transcriptOpen = replySaved = false;
    historyOffset = 0;
    awaitingSubmit = false;
    newMessage = noticeRinging = false;
    noticeId = "";
    level = 0;
}

void fail(const String& message) {
    resetAudio();
    error = message; show(State::Error);
    if (gatewayReady) socket.sendTXT("hangup");
}

void hangup() {
    if (!inCall && (state == State::Speaking || state == State::Waiting)) saveCurrentReply();
    if (gatewayReady && noticeId.length()) socket.sendTXT("notice-done:" + noticeId);
    resetAudio();
    if (gatewayReady) socket.sendTXT("hangup");
    show(State::Idle);
}

bool canReceiveNotice() {
    return (state == State::Idle || state == State::Reply || state == State::Error) &&
           !settingsOpen && !M5.BtnA.isPressed() && !M5.BtnB.isPressed() &&
           !M5.Touch.getDetail().isPressed();
}

void startNotice(const String& id) {
    if (!canReceiveNotice() || id.isEmpty() || id.length() > 20) return;
    resetAudio();
    newMessage = noticeRinging = true;
    noticeId = id;
    reply = "";
    scroll = 0;
    noticeWokeScreen = screenAsleep;
    if (screenAsleep) M5.Display.wakeup();
    screenAsleep = false;
    lastFrame = 0;
    lastScreenActivity = millis();
    show(State::Speaking);
    noticeRingAt = noticeTextAt = noticeVoiceAt = 0;
    noticeChimeQueued = queueNoticeChime(playback);
    if (!noticeChimeQueued) fail(ANNA_TEXT("Chime unavailable", "提示音缓冲不可用", "通知音を再生できません"));
}

void receive(WStype_t kind, uint8_t* data, size_t count) {
    if (kind == WStype_DISCONNECTED) {
        if (gatewayReady) recordLinkDiagnostic(LinkEvent::GatewayDown);
        gatewayReady = false;
        const bool establishedCall = inCall && (state == State::Listening || state == State::Speaking);
        if (establishedCall && (!lastCallRecoveryAt || millis() - lastCallRecoveryAt >= 15000)) {
            beginCallRecovery();
        } else if (!resumeMeshCall && (state == State::Record || state == State::Waiting ||
                   state == State::Dialing || state == State::Listening || state == State::Speaking)) {
            fail(ANNA_TEXT("Mac disconnected", "与 Mac 的连接已断开", "Mac との接続が切れました"));
        }
        return;
    }
    if (kind == WStype_BIN) {
        if (state != State::Speaking || count != samples * sizeof(int16_t)) return;
        if (!playback.append(data)) fail(ANNA_TEXT("Playback buffer full", "播放缓冲已满，请重试", "再生バッファがいっぱいです"));
        return;
    }
    if (kind != WStype_TEXT || count > 2000) return;
    String message;
    message.concat(reinterpret_cast<const char*>(data), count);
    if (message == "ready") {
        gatewayReady = true;
        linkDiagnosticSentAt = 0;
        recordLinkDiagnostic(LinkEvent::GatewayReady);
        if (resumeMeshCall) start(true, true);
    } else if (message.startsWith("wifi-diag-ack:")) {
        acknowledgeLinkDiagnostic(strtoul(message.c_str() + 14, nullptr, 10));
    }
    else if (message.startsWith("notice:")) {
        startNotice(message.substring(7));
    } else if (message.startsWith("notice-text:") && noticeId.length() && !noticeRinging) {
        if (!noticeTextAt) noticeTextAt = millis();
        if (reply.length() < 24000) reply += message.substring(12);
    }
    else if (message == "connected" && state == State::Dialing) {
        callSince = millis(); show(State::Listening);
    } else if (message == "submitted" && state == State::Waiting) {
        awaitingSubmit = false;
        since = millis();
    } else if (message == "preparing" && state == State::Waiting) {
        preparingReply = true;
        since = millis();
    } else if (message == "speaking" && (state == State::Waiting || state == State::Listening)) {
        preparingReply = false;
        awaitingSubmit = false;
        endMic();
        playbackGaps = 0;
        show(State::Speaking);
    } else if (message == "listening" && inCall && state == State::Speaking) {
        resumeListening = true;
        playback.finish();
    } else if (message == "done" && !inCall && (state == State::Speaking || state == State::Waiting)) {
        saveCurrentReply();
        finishReply = true;
        playback.finish();
    } else if (message.startsWith("reply-view:") && !inCall &&
               (state == State::Speaking || state == State::Waiting)) {
        awaitingSubmit = false;
        reply = message.substring(11);
    } else if (message.startsWith("reply:") && !inCall &&
               (state == State::Speaking || state == State::Waiting)) {
        String text = message.substring(6);
        if (reply.length() && reply.length() < 1600) reply += '\n';
        if (reply.length() < 1600) reply += text;
    } else if (message.startsWith("error:") && (inCall || state == State::Waiting ||
               state == State::Record || state == State::Speaking)) {
        fail(message.substring(6));
    }
}

void start(bool call, bool recovering) {
    if (!gatewayReady) { fail(ANNA_TEXT("Mac disconnected", "Mac 服务尚未连接", "Mac に未接続")); return; }
    resetAudio();
    reply = ""; scroll = 0;
    micFrames = 0; micPeak = 0;
    if (!recovering) {
        lastCallRecoveryAt = 0;
        callScanIndex = 0;
    }
    inCall = call;
    show(call ? State::Dialing : State::Record);
    if (!socket.sendTXT(call ? "call" : "single")) fail(ANNA_TEXT("Could not send request", "请求未发出，请重试", "送信できませんでした"));
}

bool startMic() {
    M5.Speaker.end();
    if (!M5.Mic.begin() || !M5.Mic.record(mic[0], 0, sampleRate)) return false;
    // Settle I2S at 16 kHz before arming the StopWatch ES8311 ADC.
    constexpr uint8_t codec = 0x18;
    constexpr uint32_t clockHz = 100000;
    if (!M5.In_I2C.writeRegister8(codec, 0x14, 0x17, clockHz) ||
        !M5.In_I2C.writeRegister8(codec, 0x17, 0xcb, clockHz) ||
        !M5.In_I2C.writeRegister8(codec, 0x1c, 0x64, clockHz)) return false;
    for (int i = 0; i < 3; ++i) {
        delay(30);
        if (!M5.In_I2C.writeRegister8(codec, 0x0d, 0x01, clockHz)) return false;
    }
    return true;
}

void microphone() {
    const bool listening = state == State::Record || state == State::Listening;
    if (!listening) return;
    if (state == State::Record && captureCapped && !released) return;
    if (!recording) {
        if (!released) {
            if (!startMic()) {
                M5.Mic.end();
                fail(ANNA_TEXT("Microphone unavailable", "麦克风未启动", "マイクを使用できません"));
                return;
            }
            recording = true;
        }
    }
    size_t stillRecording = recording ? M5.Mic.isRecording() : 0;
    // isRecording() briefly reports zero before the worker accepts a new buffer.
    if (!stillRecording && millis() - lastMicQueue < 8) stillRecording = recordCount;
    while (recordCount > stillRecording) {
        int peak = 0;
        for (const auto value : mic[recordHead]) peak = std::max(peak, abs(int(value)));
        level = (level + peak) / 2;
        micPeak = std::max(micPeak, peak);
        for (size_t i = 0; i < captureSamples; i += samples) {
            if (!socket.sendBIN(reinterpret_cast<const uint8_t*>(mic[recordHead] + i), samples * sizeof(int16_t))) {
                fail(ANNA_TEXT("Recording disconnected", "录音连接已断开", "録音の接続が切れました")); return;
            }
            ++micFrames;
        }
        recordHead = (recordHead + 1) % 2;
        --recordCount;
    }
    if (released && state == State::Record) {
        if (!recordCount) {
            endMic();
            awaitingSubmit = true;
            show(State::Waiting);
            if (!socket.sendTXT("release")) fail(ANNA_TEXT("Recording not sent", "录音未送达，请重试", "録音を送信できませんでした"));
        }
        return;
    }
    if (state == State::Record && millis() - since > 14500) {
        if (!recordCount) { endMic(); captureCapped = true; }
        return;
    }
    if (recordCount < 2 &&
        M5.Mic.record(mic[(recordHead + recordCount) % 2], captureSamples, sampleRate)) {
        ++recordCount;
        lastMicQueue = millis();
    }
}

void speaker() {
    if (state != State::Speaking && !finishReply) return;
    const bool ending = noticeRinging || resumeListening || finishReply;
    if (!playing && playback.ready(ending)) {
        M5.Mic.end();
        M5.Speaker.setVolume(volume);
        playing = M5.Speaker.begin();
        if (!playing) { fail(ANNA_TEXT("Speaker unavailable", "扬声器未启动", "スピーカーを使用できません")); return; }
    }
    if (playing) {
        const size_t native = M5.Speaker.isPlaying(uint8_t(0));
        const bool drained = !native && playback.submitted && !ending;
        playback.retire(native);
        if (drained) {
            // Resume with queued call audio before waiting for a fresh prefill.
            buffering = !inCall || !playback.canSubmit();
            ++playbackGaps;
        }
        if (playback.ready(ending)) buffering = false;
        while (!buffering && playback.canSubmit()) {
            const auto* block = playback.front();
            const size_t size = playback.frontSize();
            if (!noticeRinging && !speechGainSet) {
                // Leave headroom at the codec, downstream of the speaker volume slider.
                constexpr uint8_t codec = 0x18, dacVolume = 0x32;
                const uint8_t speechGain = inCall ? 0xf4 : 0xe3;
                constexpr uint32_t clockHz = 100000;
                M5.In_I2C.writeRegister8(codec, dacVolume, speechGain, clockHz);
                speechDacGain = M5.In_I2C.readRegister8(codec, dacVolume, clockHz);
                speechGainSet = true;
            }
            if (!M5.Speaker.playRaw(block, size, sampleRate, false, 1, 0)) break;
            if (!inCall && !replyHapticSent) {
                M5.Power.setVibration(150);
                replyHapticAt = millis();
                replyHapticSent = replyHapticActive = true;
                ++replyHapticCount;
            }
            if (noticeRinging && !noticeRingAt) noticeRingAt = millis();
            if (!noticeRinging && noticeId.length() && !noticeVoiceAt) noticeVoiceAt = millis();
            int peak = 0;
            for (size_t i = 0; i < size; ++i) peak = std::max(peak, abs(int(block[i])));
            level = (level + peak) / 2;
            playback.submit();
        }
    }
    if (playback.empty() && ending) {
        // Native queue consumption precedes the final samples leaving I2S DMA.
        if (playing) {
            if (!playbackDrainAt) playbackDrainAt = millis();
            if (millis() - playbackDrainAt < 100) return;
        }
        if (noticeRinging) {
            noticeRinging = false;
            playbackDrainAt = 0;
            buffering = true;
            socket.sendTXT("notice-accept:" + noticeId);
            since = millis();
            return;
        }
        endPlayback();
        if (finishReply) {
            saveCurrentReply();
            finishReply = false;
            if (noticeId.length()) {
                socket.sendTXT("notice-done:" + noticeId);
                noticeId = "";
                ++noticesDone;
            } else socket.sendTXT("hangup");
            if (replyDismissed) {
                replyDismissed = false;
                show(State::Idle);
            } else if (reply.length()) show(State::Reply);
            else fail(ANNA_TEXT("No text reply", "未收到回复文字，请重试", "テキスト返信がありません"));
        } else {
            resumeListening = false;
            show(State::Listening);
        }
    }
}

void textStyle(uint32_t color = white, float size = 1.f) {
    frame.setFont(uiFont);
    frame.setTextColor(color);
    frame.setTextSize(size);
}

void title(const char* text, int y, uint32_t color = white, float size = 1.f) {
    textStyle(color, size);
    frame.drawCenterString(text, center, y);
}

void avatar(float x, float y, float scale, float blink, float mouth, float gaze,
            bool dizzy = false, bool wink = false, float tilt = 0.f, bool smilingEyes = false,
            bool confused = false) {
    body.pushRotateZoom(&frame, x, y, tilt, scale, scale, navy);
    const float size = 208.f * scale;
    const float radians = tilt * .017453293f;
    const float cosine = cosf(radians), sine = sinf(radians);
    auto X = [&](float u, float v) { return int(x + size * ((u - .5f) * cosine - (v - .5f) * sine)); };
    auto Y = [&](float u, float v) { return int(y + size * ((u - .5f) * sine + (v - .5f) * cosine)); };
    const uint32_t dark = 0x251713;
    for (float eye : {.385f, .57f}) {
        if (smilingEyes) {
            float previousU = eye + gaze - .045f, previousV = .414f;
            for (int step = 1; step <= 8; ++step) {
                const float phase = step / 8.f;
                const float u = eye + gaze + .09f * (phase - .5f);
                const float v = .414f - .035f * sinf(phase * PI);
                frame.drawWideLine(X(previousU, previousV), Y(previousU, previousV),
                                   X(u, v), Y(u, v), std::max(2.f, size * .022f), dark);
                previousU = u;
                previousV = v;
            }
            continue;
        }
        const bool leftEye = eye < .5f;
        float eyelid = wink && leftEye ? .13f : blink;
        if (confused) {
            eyelid *= leftEye ? .57f : 1.08f;
            const float innerY = leftEye ? .313f : .274f;
            const float outerY = leftEye ? .292f : .298f;
            const float sign = leftEye ? 1.f : -1.f;
            frame.drawWideLine(X(eye - sign * .042f, outerY), Y(eye - sign * .042f, outerY),
                               X(eye + sign * .039f, innerY), Y(eye + sign * .039f, innerY),
                               size * .018f, dark);
        }
        frame.fillEllipse(X(eye + gaze, .395f), Y(eye + gaze, .395f), std::max(1, int(size * .055f)),
                          std::max(1, int(size * .062f * eyelid)), dark);
        if (dizzy) {
            for (int i = 0; i < 24; ++i) {
                const float angle = i * .48f, r = size * i * .0018f;
                frame.fillCircle(X(eye + gaze, .395f) + int(cosf(angle) * r), Y(eye + gaze, .395f) + int(sinf(angle) * r), 1, 0xffe4bdu);
            }
        } else if (eyelid > .4f) {
            frame.fillCircle(X(eye + gaze - .019f, .374f), Y(eye + gaze - .019f, .374f), std::max(1, int(size * .017f)), 0xfff7dfu);
        }
    }
    const int radius = int(size * .086f);
    for (int i = 0; i < std::max(1, int(size * .021f)); ++i)
        frame.drawCircle(X(.57f, .395f), Y(.57f, .395f), radius + i, dark);
    frame.drawWideLine(X(.647f, .44f), Y(.647f, .44f), X(.679f, .515f), Y(.679f, .515f), std::max(1.f, size * .024f), dark);
    frame.drawWideLine(X(.679f, .515f), Y(.679f, .515f), X(.765f, .566f), Y(.765f, .566f), std::max(1.f, size * .024f), dark);
    if (mouth > 0.f) frame.fillEllipse(X(.47f, .55f), Y(.47f, .55f), std::max(1, int(size * .037f)),
                                       std::max(1, int(size * .038f * mouth)), dark);
}

constexpr float idlePlayfieldX = 142.f, idlePlayfieldY = 123.f, idlePlayfieldCenterY = 245.f;
void constrainIdlePosition(float& x, float& y) {
    const float dx = (x - center) / idlePlayfieldX;
    const float dy = (y - idlePlayfieldCenterY) / idlePlayfieldY;
    const float radius = std::hypot(dx, dy);
    if (radius > 1.f) {
        x = center + (x - center) / radius;
        y = idlePlayfieldCenterY + (y - idlePlayfieldCenterY) / radius;
    }
}

void drawIdleAvatar(float x, float y, float scale, float blink, float mouth, float gaze,
                    bool dizzy = false, bool wink = false) {
    float tilt = 0.f;
    bool smilingEyes = idleRubbing;
    // The compact idle face needs more motion to read clearly at a glance.
    const float movement = scale <= 1.f ? 1.35f : 1.f;
    const uint32_t elapsed = millis() - idleReactionAt;
    if (scale <= 1.f && !idleRubbing && idleTravel != IdleTravel::None) {
        constexpr uint32_t flightMs = 310, dazedMs = 1150, returnMs = 340;
        const uint32_t age = millis() - idleTravelAt;
        const bool thrown = idleTravel == IdleTravel::Throw;
        const uint32_t returnAt = thrown ? flightMs + dazedMs : 0;
        if (age >= returnAt + returnMs) {
            idleTravel = IdleTravel::None;
        } else {
            float px = idleDragX, py = idleDragY;
            if (thrown) {
                const float progress = std::min(1.f, age / float(flightMs));
                const float travel = idleFlightDistance * progress * (2.f - progress);
                px += idleVelocityX * travel;
                py += idleVelocityY * travel;
                constrainIdlePosition(px, py);
                const float turn = idleVelocityX < 0.f ? -1.f : 1.f;
                tilt = turn * 82.f * progress;
                mouth = .45f;
                gaze = 0.f;
                dizzy = age >= flightMs;
                if (dizzy) {
                    const float timeSinceLanding = age - flightMs;
                    tilt += turn * 9.f * sinf(timeSinceLanding * .039f) * expf(-.008f * timeSinceLanding);
                }
            }
            const float back = age < returnAt ? 0.f : (age - returnAt) / float(returnMs);
            const float blend = back * back * (3.f - 2.f * back);
            x = px + (x - px) * blend;
            y = py + (y - py) * blend;
            tilt *= 1.f - blend;
            if (thrown && dizzy) {
                const float orbit = age * .007f;
                for (int i = 0; i < 3; ++i) {
                    const float angle = orbit + i * 2.0944f;
                    frame.fillCircle(int(x + 38.f * cosf(angle)), int(y - 68.f + 9.f * sinf(angle)), 3, apricot);
                }
            }
        }
    } else if (idleRubbing) {
        if (scale <= 1.f) {
            x = idleDragX;
            y = idleDragY;
        }
        tilt = movement * (idleRubTilt + 2.5f * sinf(millis() * .033f));
        x += tilt * 1.15f;
        y -= 4.f * fabsf(sinf(millis() * .025f));
        mouth = .75f + .19f * sinf(millis() * .042f);
        dizzy = wink = false;
    } else if (idleTaps && elapsed < idleReactionMs) {
        const float phase = float(elapsed) / idleReactionMs;
        const float envelope = sinf(phase * PI);
        dizzy = wink = false;
        switch (idleReaction) {
            case IdleReaction::Cuddle:
                tilt = 12.f * movement * envelope;
                blink = 1.f - .88f * envelope;
                x += 13.f * movement * envelope;
                break;
            case IdleReaction::Tickle:
                x += 13.f * movement * envelope * sinf(phase * 12.f * PI);
                smilingEyes = envelope > .22f;
                mouth = .95f * envelope;
                break;
            case IdleReaction::Hop:
                y -= 31.f * movement * envelope * fabsf(sinf(phase * 3.f * PI));
                wink = phase > .4f && phase < .64f;
                mouth = .6f * envelope;
                break;
        }
    }
    idleFaceX = x;
    idleFaceY = y;
    idleFaceRadius = 91.f * scale;
    avatar(x, y, scale, blink, mouth, gaze, dizzy, wink, tilt, smilingEyes);
}

void clockFace(int y, float size) {
    time_t now = time(nullptr);
    tm local;
    localtime_r(&now, &local);
    char timeText[8] = "--:--";
    if (now > 1700000000) snprintf(timeText, sizeof(timeText), "%02d:%02d", local.tm_hour, local.tm_min);
    title(timeText, y, white, size);
}

void drawVoiceButton(const char* label) {
    constexpr int radius = voiceButtonH / 2;
    lgfx::argb8888_t tint[voiceButtonW];
    std::fill_n(tint, voiceButtonW, lgfx::argb8888_t(56, 24, 12, 20));
    for (int row = 0; row < voiceButtonH; ++row) {
        const float dy = row + .5f - radius;
        const int inset = radius - int(sqrtf(radius * radius - dy * dy));
        frame.pushAlphaImage(voiceButtonX + inset, voiceButtonY + row, voiceButtonW - 2 * inset, 1, tint);
    }
    frame.drawRoundRect(voiceButtonX, voiceButtonY, voiceButtonW, voiceButtonH, radius, 0xd3b5acu);
    title(label, voiceButtonY + 21, white, 1.05f);
}

void status() {
    const int strength = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -110;
    (strength > -100 ? wifiOn : wifiOff).pushSprite(108, 75);
    clockFace(63, 1.2f);
    const int battery = M5.Power.getBatteryLevel();
    batteryIcon.pushSprite(307, 76);
    if (battery > 0) frame.fillRoundRect(311, 81, std::max(1, std::clamp(battery, 0, 100) * 20 / 100), 8, 2, muted);
    textStyle(muted, .78f);
    char value[7]; snprintf(value, sizeof(value), battery < 0 ? "--" : "%d%%", battery);
    frame.drawString(value, 346, 73);
}

void drawReply(float mouth) {
    // The final line still needs to fit inside the round screen.
    constexpr int left = 72, right = 394, top = 165, lineHeight = 29, rows = 7;
    const SavedReply* older = historyOffset
        ? &history[(historyNewest - historyOffset + (replySaved ? 0 : 1)) % historyLimit] : nullptr;
    const String& displayed = older ? older->text : reply;
    if (older) mouth = 0.f;
    avatar(139, 120 + (mouth > 0.f ? 2.f * sinf(millis() * .008f) : 0.f),
           .24f, 1.f, mouth, 0);
    textStyle();
    frame.drawString((older ? older->notice : newMessage) ? ANNA_TEXT("Dot message", "Dot 新消息", "Dot 新着") : ANNA_TEXT("Dot reply", "Dot 回复", "Dot の返信"), 176, 107);
    for (int y = replyTop; y < 438; ++y) {
        const int dy = y - center;
        const int edge = int(sqrtf(227.f * 227.f - dy * dy)) - 15;
        const int corner = std::max(0, 168 - y);
        const int half = std::min(172 + int(sqrtf(21.f * 21.f - corner * corner)), edge);
        frame.drawFastHLine(center - half, y, half * 2, replyBackground);
    }
    textStyle();
    int line = 0, x = left;
    lgfx::FontMetrics baseMetrics{};
    uiFont->getDefaultMetric(&baseMetrics);
    lgfx::FontMetrics fallbackMetrics{};
    bitmapFont.getDefaultMetric(&fallbackMetrics);
    const char* data = displayed.c_str();
    for (size_t i = 0; i < displayed.length();) {
        const auto byte = static_cast<uint8_t>(data[i]);
        size_t n = byte < 128 ? 1 : byte < 224 ? 2 : byte < 240 ? 3 : 4;
        if (i + n > displayed.length()) break;
        char glyph[5]{}; memcpy(glyph, data + i, n); i += n;
        uint32_t code = n == 1 ? byte : byte & (0x7fu >> n);
        for (size_t j = 1; j < n; ++j) code = (code << 6) | (uint8_t(glyph[j]) & 0x3fu);
        lgfx::FontMetrics metrics{};
        const bool fallback = code > 0xffffu || !uiFont->updateFontMetric(&metrics, code);
        frame.setFont(fallback ? &bitmapFont : uiFont);
        const int w = frame.textWidth(glyph);
        if (glyph[0] == '\n' || x + w > right) {
            ++line; x = left;
            if (glyph[0] == '\n') continue;
        }
        if (line >= scroll && line < scroll + rows) {
            const int offset = fallback ? baseMetrics.baseline - fallbackMetrics.baseline : 0;
            frame.drawString(glyph, x, top + (line - scroll) * lineHeight + offset);
        }
        x += w;
    }
    const int maxScroll = std::max(0, line + 1 - rows);
    if (maxScroll) {
        frame.fillRoundRect(407, 182, 3, 150, 1, slate);
        frame.fillRoundRect(407, 182 + std::min(scroll, maxScroll) * 122 / maxScroll, 3, 28, 1, blue);
    }
    scroll = std::clamp(scroll, 0, maxScroll);
    if (previousReplyCount()) {
        char page[12];
        snprintf(page, sizeof(page), "%u/%u", historyOffset + 1, previousReplyCount() + 1);
        title(page, 401, muted, .68f);
    }
}

void drawSettings() {
    title(ANNA_TEXT("Settings", "设置", "設定"), 109, muted, .86f);
    frame.fillRoundRect(72, 137, 322, 114, 19, replyBackground);
    auto slider = [&](const char* name, int y, int percent) {
        textStyle(white, .86f);
        frame.drawString(name, settingsTrackX, y - 36);
        char value[8];
        snprintf(value, sizeof(value), "%d%%", percent);
        textStyle(muted, .8f);
        frame.drawRightString(value, settingsTrackX + settingsTrackW, y - 35);
        frame.fillRoundRect(settingsTrackX, y - 3, settingsTrackW, 6, 3, slate);
        const int span = settingsTrackW * percent / 100;
        if (span > 0) frame.fillRoundRect(settingsTrackX, y - 3, span, 6, std::min(3, span / 2), apricot);
        frame.fillCircle(settingsTrackX + span, y, 9, 0xffdfcbu);
    };
    slider(ANNA_TEXT("Volume", "音量", "音量"), volumeSliderY, volume * 100 / 255);
    slider(ANNA_TEXT("Brightness", "亮度", "明るさ"), brightnessSliderY, (brightness - 12) * 100 / 243);
    const String ssid = wifiSwitchPending ? (tryingHotspot ? hotspotSsid : homeSsid) :
                        WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String();
    const char* networkLabel = wifiSwitchPending ? ANNA_TEXT("Connecting", "正在连接", "接続中") :
                               ssid.isEmpty() ? ANNA_TEXT("Wi-Fi offline", "Wi-Fi 未连接", "Wi-Fi 未接続") :
                               isHotspot(ssid) ? ANNA_TEXT("Phone hotspot", "手机热点", "テザリング") : ANNA_TEXT("Home Wi-Fi", "家里网络", "自宅 Wi-Fi");
    frame.fillRoundRect(wifiButtonX, settingsCardY, settingsCardW, settingsCardH, 16, replyBackground);
    frame.fillRoundRect(screenModeX, settingsCardY, settingsCardW, settingsCardH, 16, replyBackground);
    const int networkTextX = wifiButtonX + 12;
    textStyle(muted, .76f);
    frame.drawString(networkLabel, networkTextX, settingsCardY + (ssid.isEmpty() ? 20 : 8));
    if (!ssid.isEmpty()) {
        constexpr float nameSize = .85f;
        const int maxNameWidth = settingsCardW - (canSwitchWifi() ? 36 : 24);
        textStyle(white, nameSize);
        const int nameWidth = frame.textWidth(ssid.c_str());
        if (nameWidth > maxNameWidth) textStyle(white, nameSize * maxNameWidth / nameWidth);
        frame.drawString(ssid.c_str(), networkTextX, settingsCardY + 32);
    }
    if (canSwitchWifi()) {
        const int arrowX = wifiButtonX + settingsCardW - 16;
        const int arrowY = settingsCardY + settingsCardH / 2;
        frame.drawWideLine(arrowX - 4, arrowY - 6, arrowX + 2, arrowY, 2.f, blue);
        frame.drawWideLine(arrowX + 2, arrowY, arrowX - 4, arrowY + 6, 2.f, blue);
    }
    const int screenTextX = screenModeX + 12;
    textStyle(muted, .76f);
    frame.drawString(ANNA_TEXT("Screen", "屏幕", "画面"), screenTextX, settingsCardY + 8);
    textStyle(alwaysOn ? apricot : white, .85f);
    frame.drawString(alwaysOn ? ANNA_TEXT("Always on", "常亮", "常時点灯") : ANNA_TEXT("Sleep in 30s", "30 秒熄屏", "30 秒で消灯"), screenTextX, settingsCardY + 32);
    const int toggleX = screenModeX + settingsCardW - 44;
    const int toggleY = settingsCardY + 21;
    frame.fillRoundRect(toggleX, toggleY, 32, 18, 9, alwaysOn ? apricot : slate);
    frame.fillCircle(toggleX + (alwaysOn ? 23 : 9), toggleY + 9, 6, alwaysOn ? navy : muted);
    frame.fillRoundRect(settingsBackX, settingsBackY, settingsBackW, settingsBackH, 23, slate);
    title(ANNA_TEXT("Back", "返回", "戻る"), settingsBackY + 12, white, .85f);
}

void dismissSettings() {
    settingsOpen = false;
    settingsDismissTouch = true;
    gestureY = -1;
    lastFrame = 0;
    ++settingsDismisses;
}

bool settingsGesture(const m5::Touch_Class::touch_detail_t& touch, uint32_t now) {
    if (settingsDismissTouch) {
        if (touch.isReleased()) settingsDismissTouch = false;
        return true;
    }
    if (touch.wasPressed()) {
        gestureY = touch.y;
        if (settingsOpen && touch.y >= settingsBackY - 6 &&
            touch.x >= settingsBackX - 18 && touch.x < settingsBackX + settingsBackW + 18) {
            dismissSettings();
            return true;
        }
        if (settingsOpen && touch.x >= settingsTrackX - 18 && touch.x <= settingsTrackX + settingsTrackW + 18) {
            if (abs(touch.y - volumeSliderY) < 22) activeSlider = 1;
            if (abs(touch.y - brightnessSliderY) < 22) activeSlider = 2;
        }
    }
    if (touch.isPressed() && activeSlider) {
        const int span = std::clamp(int(touch.x) - settingsTrackX, 0, settingsTrackW);
        if (activeSlider == 1) {
            const uint8_t next = span * 255 / settingsTrackW;
            if (next != volume) {
                M5.Speaker.setVolume(volume = next);
                lastFrame = 0;
            }
        } else {
            const uint8_t next = 12 + span * 243 / settingsTrackW;
            if (next != brightness) {
                M5.Display.setBrightness(brightness = next);
                lastFrame = 0;
            }
        }
    } else if (touch.isPressed() && gestureY >= 0) {
        if (!settingsOpen && gestureY < 105 && touch.y - gestureY > 44) {
            settingsOpen = true;
            gestureY = -1;
            lastFrame = 0;
        } else if (settingsOpen && abs(touch.distanceX()) >= 66 && abs(touch.distanceY()) <= 38) {
            dismissSettings();
            return true;
        }
    }
    if (touch.wasReleased()) {
        if (settingsOpen && !activeSlider && abs(touch.distanceX()) <= 20 && abs(touch.distanceY()) <= 20) {
            const int x = touch.base_x, y = touch.base_y;
            const bool onCardRow = y >= settingsCardY && y < settingsCardY + settingsCardH;
            if (onCardRow && x >= screenModeX && x < screenModeX + settingsCardW) {
                alwaysOn = !alwaysOn;
                Provision::storage.putBool("always-on", alwaysOn);
                lastScreenActivity = now;
                lastFrame = 0;
            } else if (onCardRow && canSwitchWifi() && x >= wifiButtonX && x < wifiButtonX + settingsCardW) {
                switchSavedWifi(!tryingHotspot);
            }
        }
        if (activeSlider == 1) Provision::storage.putUChar("volume", volume);
        if (activeSlider == 2) Provision::storage.putUChar("brightness", brightness);
        activeSlider = 0;
        gestureY = -1;
    }
    return settingsOpen || (gestureY >= 0 && gestureY < 105);
}

void draw() {
    frame.fillScreen(navy);
    const float t = millis() / 1000.f;
    const float cycle = fmodf(t, 16.f);
    const State view = replyDismissed ? State::Idle : state;
    const bool call = view == State::Dialing || inCall;
    const bool speaking = view == State::Speaking;
    const bool thinking = (view == State::Record && released) ||
                          (view == State::Waiting && awaitingSubmit);
    const bool listening = view == State::Listening || (view == State::Record && !released);
    const bool largeFace = alwaysOn && view == State::Idle && !settingsOpen && !Provision::active;
    const bool largeVoice = largeVoiceView() && !settingsOpen && !Provision::active;
    if (!largeFace && !largeVoice) status();
    if (Provision::active) {
        if (Provision::connectedAt || Provision::joining) {
            avatar(center, 203, .8f, 1.f, 0, 0);
            title(Provision::connectedAt ? ANNA_TEXT("Wi-Fi connected", "Wi-Fi 已连接", "Wi-Fi 接続済み") :
                  Provision::hotspot ? ANNA_TEXT("Start phone hotspot", "请打开手机热点", "テザリングをオンに") : ANNA_TEXT("Connecting to Wi-Fi", "正在连接 Wi-Fi", "Wi-Fi に接続中"), 303);
        } else {
            title(ANNA_TEXT("Scan to set up", "手机扫码配网", "QR コードで設定"), 110);
            frame.fillRoundRect(145, 151, 176, 176, 8, white);
            const String code = String("WIFI:T:WPA;S:Dot-Setup;P:") + setupPassword + ";;";
            frame.qrcode(code.c_str(), 151, 157, 164, 4, true);
            title("192.168.4.1", 345, blue);
        }
    } else if (settingsOpen) {
        drawSettings();
    } else if (largeFace) {
        clockFace(34, 1.12f);
        const float play = fmodf(t, 8.f);
        const float hop = play > 5.f ? abs(sinf((play - 5.f) * 3.1416f)) : 0.f;
        const float blink = play < .16f ? .13f + .87f * abs(play - .08f) / .08f : 1.f;
        drawIdleAvatar(center + 12.f * sinf(t * 1.7f), 252 - 13.f * hop, width * .8f / 208.f,
               blink, play > 5.f ? .25f + .22f * hop : 0.f, .025f * sinf(t * 2),
               false, play > 3.8f && play < 4.09f);
    } else if (view == State::Error) {
        const float seconds = (millis() - since) * .001f;
        const float sway = sinf(seconds * 5.f);
        const float scale = alwaysOn ? 1.32f : 1.f;
        avatar(center, 218.f + 3.f * sway, scale, 1.f, .40f,
               .020f * sinf(seconds * 3.f), false, false, 9.f + 6.f * sway, false, true);
        textStyle(yellow);
        const int errorWidth = frame.textWidth(error.c_str());
        const float errorScale = errorWidth > 300 ? 300.f / errorWidth : 1.f;
        title(error.c_str(), 365, yellow, errorScale);
    } else if (largeVoice) {
        clockFace(34, 1.12f);
        const float blinkPhase = fmodf(t, 4.9f);
        const float blink = blinkPhase < .14f ? .12f + .88f * fabsf(blinkPhase - .07f) / .07f : 1.f;
        const float mouth = speaking ? std::clamp(level / 3000.f, 0.f, 1.f) : 0.f;
        const bool waiting = view == State::Waiting;
        const float sway = sinf(t * (listening ? 4.f : 2.5f));
        avatar(center, 212.f - (thinking ? 7.f : 3.f) * sway - 4.f * mouth,
               1.68f + (listening ? .025f * sway : 0.f), blink, mouth,
               thinking || waiting ? .023f * sway : 0.f, false, false,
               listening ? -4.f + 1.5f * sway : 0.f);
        const char* label = thinking ? ANNA_TEXT("Thinking", "思考中", "考え中") : speaking ? ANNA_TEXT("Dot speaking", "Dot 在说", "Dot が話しています") :
                            listening ? ANNA_TEXT("Listening", "正在听", "聞いています") : waiting ? ANNA_TEXT("Waiting for reply", "等待回复", "返信待ち") :
                            view == State::Dialing ? (resumeMeshCall ? ANNA_TEXT("Reconnecting", "正在重连", "再接続中") : ANNA_TEXT("Calling", "正在拨号", "呼び出し中")) : ANNA_TEXT("Reply", "回复", "返信");
        title(label, 371, apricot, .92f);
        if (view == State::Reply) drawVoiceButton(ANNA_TEXT("Read reply", "看文字", "返信を読む"));
    } else if (replyVisible()) {
        drawReply(speaking ? std::clamp(level / 3000.f, .18f, 1.f) : 0.f);
        drawnReplyScroll = scroll;
    }
    else {
        float x = center, y = call ? 193 : 240 + 4 * sinf(t * 2), gaze = 0;
        if (view == State::Idle && cycle > 10.f) {
            const float phase = (cycle - 10.f) / 6.f;
            x += sinf(phase * 6.283f) * 20.f;
            y -= abs(sinf(phase * 12.566f)) * 15.f;
            gaze = .018f * cosf(phase * 6.283f);
        }
        const bool dizzy = view == State::Idle && motion.dizzy(millis());
        if (view == State::Idle) {
            x = std::clamp(x + motion.x, 128.f, 338.f);
            y += motion.y;
        }
        if (dizzy) {
            x = std::clamp(x + 13.f * sinf(t * 19.f), 128.f, 338.f);
            y += 8.f * cosf(t * 15.f);
            gaze = 0;
        }
        const float blinkPhase = fmodf(t, 4.9f);
        const float blink = blinkPhase < .14f ? .12f + .88f * fabsf(blinkPhase - .07f) / .07f : 1.f;
        const float mouth = speaking && level > 0 ? std::clamp(level / 3000.f, .18f, 1.f) : 0.f;
        const float scale = call ? .76f : listening ? 1.04f + .025f * sinf(t * 8.f) : 1.f;
        if (thinking) {
            y -= 6.f + 5.f * sinf(t * 4.f);
            gaze = .022f * sinf(t * 2.f);
        } else if (speaking) {
            y -= 4.f * mouth;
        }
        if (view == State::Idle && state == State::Idle)
            drawIdleAvatar(x, y, scale, dizzy ? 1.f : blink, dizzy ? .58f : mouth, gaze, dizzy);
        else avatar(x, y, scale, blink, mouth, gaze);
        if (thinking || (listening && !call)) {
            for (int i = -2; i <= 2; ++i) {
                const int height = int(3.f + (thinking ? 2.f : 6.f) *
                    (1.f + sinf(t * 9.f - i * .8f)));
                frame.fillRoundRect(center + i * 16 - 3, 322 - height / 2, 6, height, 3,
                                    thinking ? apricot : blue);
            }
        }
        if (view == State::Dialing) {
            for (int i = 0; i < 5; ++i) {
                const float angle = t * 3.1f - i * .29f;
                const float size = .92f - i * .13f;
                bead.pushRotateZoom(&frame, center + 85.f * cosf(angle), 193 + 85.f * sinf(angle),
                                    0.f, size, size, navy);
            }
        }
        if (dizzy) {
            for (int i = 0; i < 3; ++i) {
                const float angle = t * 4.f + i * 2.0944f;
                const int sx = int(x + 86.f * cosf(angle)), sy = int(y - 92.f + 17.f * sinf(angle));
                bead.pushRotateZoom(&frame, sx, sy, 0.f, .56f, .56f, navy);
            }
        }
        if (thinking) title(ANNA_TEXT("Thinking", "思考中", "考え中"), 351, yellow);
        else if (view == State::Record) title(captureCapped ? ANNA_TEXT("Release to send", "松开黄键发送", "離して送信") : ANNA_TEXT("Listening", "正在听", "聞いています"), 351, yellow);
        else if (view == State::Waiting) title(ANNA_TEXT("Waiting for reply", "等待回复", "返信待ち"), 351, muted);
        else if (call) {
            if (view == State::Dialing) {
                dial.pushSprite(169, 280);
                textStyle();
                frame.drawString(resumeMeshCall ? ANNA_TEXT("Reconnecting", "正在重连", "再接続中") : ANNA_TEXT("Calling", "正在拨号", "呼び出し中"), 213, 285);
            }
            else {
                const int magnitude = std::clamp(level / 240, 2, 17);
                for (int i = -4; i <= 4; ++i) {
                    const int h = 5 + std::max(0, magnitude - abs(i) * 2);
                    frame.fillRoundRect(center + i * 11 - 2, 282 - h / 2, 5, h, 2, blue);
                }
                title(speaking ? ANNA_TEXT("Dot speaking", "Dot 在说", "Dot が話しています") : ANNA_TEXT("Call · Listening", "通话中 · 正在听", "通話中 · 聴取中"), 302);
                const unsigned seconds = (millis() - callSince) / 1000;
                char elapsed[12];
                snprintf(elapsed, sizeof(elapsed), "%02u:%02u", seconds / 60, seconds % 60);
                title(elapsed, 334);
            }
        }
    }
    M5.Display.getPanel()->getBus()->wait();
    frame.pushSprite(0, 0);
    // Present the complete frame at the StopWatch AMOLED's TE rising edge.
    const uint32_t syncStarted = micros();
    while (digitalRead(38) && micros() - syncStarted < 22000) delayMicroseconds(10);
    while (!digitalRead(38) && micros() - syncStarted < 22000) delayMicroseconds(10);
    if (micros() - syncStarted < 22000) ++panelSyncs;
    M5.Display.display();
    M5.Display.getPanel()->getBus()->wait();
    ++presentedFrames;
}
}

void setup() {
    Serial.begin(115200);
    loadReplyHistory();
    M5.begin(M5.config());
    M5.Display.setRotation(0);
    Provision::storage.begin("anna-wifi", false);
    alwaysOn = Provision::storage.getBool("always-on", false);
    volume = Provision::storage.getUChar("volume", 128);
    brightness = std::max(uint8_t(12), Provision::storage.getUChar("brightness", 95));
    M5.Display.setBrightness(brightness);
    M5.Display.setAutoDisplay(false);
    frame.setColorDepth(16);
    frame.setPsram(true);
    body.setColorDepth(16);
    body.setPsram(true);
    if (!frame.createSprite(width, width) || !body.createSprite(208, 208)) {
        M5.Display.println("ANNA display unavailable");
        return;
    }
    body.setPivot(104, 104);
    body.drawPng(watchBody, sizeof(watchBody), 0, 0);
    auto loadArt = [](M5Canvas& sprite, int w, int h, const uint8_t* png, size_t len) {
        sprite.setColorDepth(16);
        if (!sprite.createSprite(w, h)) return;
        sprite.setPivot(w / 2, h / 2);
        sprite.drawPng(png, len, 0, 0);
    };
    loadArt(dial, 37, 31, watchDial, sizeof(watchDial));
    loadArt(wifiOn, 28, 19, watchWifi, sizeof(watchWifi));
    loadArt(wifiOff, 28, 19, watchWifiOffline, sizeof(watchWifiOffline));
    loadArt(batteryIcon, 33, 18, watchBattery, sizeof(watchBattery));
    loadArt(bead, 24, 24, watchBead, sizeof(watchBead));
    if (smoothFont.loadFont(&uiFontData)) uiFont = &smoothFont;
    M5.Mic.setSampleRate(sampleRate);
    playback.storage = static_cast<int16_t (*)[AnnaPlayback::chunkSamples]>(
        ps_calloc(AnnaPlayback::capacity, sizeof(*playback.storage)));
    linkDiagnosticBoot = esp_random();
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        recordLinkDiagnostic(LinkEvent::WifiDown, info.wifi_sta_disconnected.reason);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    idleWifiSleep = WiFi.getSleep();
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    homeSsid = Provision::storage.getString("ssid");
    hotspotSsid = Provision::storage.getString("hotspot-ssid");
    preferHotspot = Provision::storage.getBool("prefer-hotspot", hotspotSsid.length() != 0);
    if (!homeSsid.length() && !hotspotSsid.length()) Provision::setup(setupPassword);
    else {
        WiFi.mode(WIFI_STA);
        connectSavedWifi(hotspotSsid.length() && (preferHotspot || !homeSsid.length()));
    }
    socket.onEvent(receive);
    socket.setReconnectInterval(1700);
    socket.setExtraHeaders(gatewayAuthorization);
    lastScreenActivity = millis();
    draw();
    Serial.println("ANNA_READY");
}

void loop() {
    if (!frame.width()) { delay(100); return; }
    M5.update();
    const uint32_t now = millis();
    if (replyHapticActive && now - replyHapticAt >= replyHapticMs) {
        M5.Power.setVibration(0);
        replyHapticActive = false;
    }
    const bool buttonDown = M5.BtnA.isPressed() || M5.BtnB.isPressed();
    if (screenAsleep && buttonDown) {
        M5.Display.wakeup();
        screenAsleep = false;
        wakePress = true;
        lastFrame = 0;
    }
    if (!buttonDown) wakePress = false;
    const auto touch = M5.Touch.getDetail();
    if (!screenAsleep && (buttonDown || touch.isPressed())) lastScreenActivity = now;
    if (Serial.available() && Serial.read() == '?') {
        Serial.printf("ANNA_STATUS setup=%u wifi=%u gateway=%u state=%u mic=%u speaker=%u hotspot=%u hotspot_saved=%u trying_hotspot=%u wifi_switches=%lu\n",
                      Provision::active, WiFi.isConnected(), gatewayReady, unsigned(state), recording, playing,
                      WiFi.isConnected() && isHotspot(WiFi.SSID()), hotspotSsid.length() != 0, tryingHotspot,
                      static_cast<unsigned long>(wifiSwitches));
        Serial.printf("ANNA_MESH rssi=%d scans=%lu roams=%lu fallbacks=%lu scanning=%u joining=%u\n",
                      WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0,
                      static_cast<unsigned long>(meshScans), static_cast<unsigned long>(meshRoams),
                      static_cast<unsigned long>(meshFallbacks), meshScanning, meshJoining);
        Serial.printf("ANNA_DISPLAY size=%dx%d frames=%lu te=%lu backdrop=%06lx setup_card=%06lx\n",
                      M5.Display.width(), M5.Display.height(), static_cast<unsigned long>(presentedFrames),
                      static_cast<unsigned long>(panelSyncs), static_cast<unsigned long>(frame.readPixel(220, 130)),
                      static_cast<unsigned long>(frame.readPixel(150, 180)));
        Serial.printf("ANNA_INPUT mic_frames=%lu mic_peak=%d imu=%u dizzy=%u\n",
                      static_cast<unsigned long>(micFrames), micPeak, M5.Imu.isEnabled(), motion.dizzy(now));
        Serial.printf("ANNA_OUTPUT buffer=%u gaps=%lu motion=%lu x=%d y=%d settings=%u settings_dismisses=%lu volume=%u brightness=%u speech_dac=%u\n",
                      playback.storage != nullptr, static_cast<unsigned long>(playbackGaps),
                      static_cast<unsigned long>(motionSamples), int(motion.x), int(motion.y),
                      settingsOpen, static_cast<unsigned long>(settingsDismisses), volume, brightness, speechDacGain);
        Serial.printf("ANNA_HISTORY saved=%u disk=%u page=%u\n",
                      unsigned(std::min<uint32_t>(historyNewest, historyLimit)), historyDisk, historyOffset);
        Serial.printf("ANNA_HAPTIC pulses=%lu active=%u\n",
                      static_cast<unsigned long>(replyHapticCount), replyHapticActive);
        Serial.printf("ANNA_UI smooth=%u\n", uiFont == &smoothFont);
        Serial.printf("ANNA_CALL reply_bytes=%u transcript=%u idle_taps=%lu reaction=%u rubs=%lu rubbing=%u throws=%lu\n",
                      unsigned(reply.length()), transcriptOpen, static_cast<unsigned long>(idleTaps),
                      unsigned(idleReaction), static_cast<unsigned long>(idleRubCount), idleRubbing,
                      static_cast<unsigned long>(idleThrows));
        Serial.printf("ANNA_SLEEP asleep=%u always_on=%u idle_ms=%lu\n", screenAsleep, alwaysOn,
                      static_cast<unsigned long>(now - lastScreenActivity));
        Serial.printf("ANNA_NOTICE woke=%u chime=%u ring=%lu text=%lu voice=%lu done=%lu\n",
                      noticeWokeScreen, noticeChimeQueued, static_cast<unsigned long>(noticeRingAt),
                      static_cast<unsigned long>(noticeTextAt), static_cast<unsigned long>(noticeVoiceAt),
                      static_cast<unsigned long>(noticesDone));
    }
    if (!wakePress && M5.BtnA.isPressed() && M5.BtnB.isPressed() && !Provision::active) {
        if (!pressedBoth) pressedBoth = now;
        if (now - pressedBoth > 2500) {
            hangup();
            socket.disconnect();
            enabled = gatewayReady = false;
            wifiSwitchPending = false;
            cancelMeshScan();
            meshJoining = false;
            Provision::setup(setupPassword);
        }
    } else pressedBoth = 0;

    if (Provision::active) Provision::tick();
    else {
        if (callWifiAwake != inCall &&
            esp_wifi_set_ps(inCall ? WIFI_PS_NONE : idleWifiSleep) == ESP_OK) {
            callWifiAwake = inCall;
        }
        const bool wifiConnected = WiFi.status() == WL_CONNECTED;
        const uint8_t* associated = meshJoining && wifiConnected ? WiFi.BSSID() : nullptr;
        const bool reachedMesh = associated && memcmp(associated, meshTarget, sizeof(meshTarget)) == 0;
        if (wifiConnected && (!meshJoining || reachedMesh) &&
            (!wifiSwitchPending || isHotspot(WiFi.SSID()) == tryingHotspot)) {
            if (meshJoining) {
                meshJoining = false;
                ++meshRoams;
                Serial.printf("ANNA_MESH joined rssi=%d\n", WiFi.RSSI());
            }
            if (!enabled) {
                meshPeakRssi = -127;
                wifiSwitchPending = false;
                configTime(8 * 3600, 0, "ntp.aliyun.com", "time.apple.com", "pool.ntp.org");
                const bool onHotspot = isHotspot(WiFi.SSID());
                retryCurrentWifi = false;
                if (onHotspot != preferHotspot) {
                    preferHotspot = onHotspot;
                    Provision::storage.putBool("prefer-hotspot", preferHotspot);
                }
                socket.beginSslWithCA(onHotspot && remoteGatewayHost[0] ? remoteGatewayHost : gatewayHost,
                                      onHotspot && remoteGatewayHost[0] ? remoteGatewayPort : 8765, "/voice", gatewayCa, "");
                enabled = true;
            }
            sampleLinkDiagnostic(now);
            if (!tickMeshRoam(now, buttonDown || touch.isPressed())) {
                socket.loop();
                sendLinkDiagnostic(millis());
            }
        } else if (meshJoining) {
            if (millis() - wifiAttemptAt >= meshConnectMs) {
                meshJoining = false;
                ++meshFallbacks;
                WiFi.disconnect(false, false);
                connectSavedWifi(false);
            }
        } else if (enabled) {
            cancelMeshScan();
            meshWeakSince = 0;
            socket.disconnect();
            enabled = false; gatewayReady = false;
            retryCurrentWifi = true;
            wifiAttemptAt = now - wifiAttemptMs;
        } else if (now - wifiAttemptAt >= wifiAttemptMs) {
            const bool useHotspot = retryCurrentWifi ? tryingHotspot :
                hotspotSsid.length() && (!homeSsid.length() || !tryingHotspot);
            retryCurrentWifi = false;
            wifiSwitchPending = false;
            WiFi.disconnect(false, false);
            connectSavedWifi(useHotspot);
        }

        if (!screenAsleep && !wakePress && !pressedBoth) {
            if (M5.BtnB.wasPressed()) {
                if (inCall) hangup();
                else if (state == State::Idle || state == State::Reply || state == State::Error) start(true);
            } else if (M5.BtnA.wasPressed()) {
                if (!inCall && state == State::Speaking) hangup();
                else if (state == State::Idle || state == State::Reply || state == State::Error) start(false);
            }
            if (state == State::Record && (M5.BtnA.wasReleased() || !M5.BtnA.isPressed()))
                released = true;
        }
        const bool usedBySettings = screenAsleep || wakePress || settingsGesture(touch, now);
        const bool largeVoice = largeVoiceView();
        const bool tap = touch.wasReleased() &&
            abs(touch.distanceX()) <= 20 && abs(touch.distanceY()) <= 20;
        const bool onVoiceButton = tap &&
            touch.base_x >= voiceButtonX - 12 && touch.base_x < voiceButtonX + voiceButtonW + 12 &&
            touch.base_y >= voiceButtonY - 10 && touch.base_y < voiceButtonY + voiceButtonH + 10;
        if (usedBySettings || state != State::Idle) {
            idleTouchCaptured = idleRubbing = false;
            idleTravel = IdleTravel::None;
        } else if (touch.wasPressed()) {
            const float dx = touch.x - idleFaceX, dy = touch.y - idleFaceY;
            idleTouchCaptured = dx * dx + dy * dy <= idleFaceRadius * idleFaceRadius;
            idleFingerX = touch.x;
            idleFingerY = touch.y;
            idleRubTilt = idleVelocityX = idleVelocityY = 0.f;
            idleFingerAt = now;
            if (idleTouchCaptured) {
                idleGrabX = idleFaceX - touch.x;
                idleGrabY = idleFaceY - touch.y;
                idleDragX = idleFaceX;
                idleDragY = idleFaceY;
                idleTravel = IdleTravel::None;
            }
        } else if (idleTouchCaptured && touch.isPressed()) {
            const int dx = touch.x - idleFingerX, dy = touch.y - idleFingerY;
            if (abs(dx) + abs(dy) >= 4) {
                const int distance = abs(touch.distanceX()) + abs(touch.distanceY());
                if (!idleRubbing && distance >= 10) {
                    idleRubbing = true;
                    ++idleRubCount;
                    idleReactionAt = now - idleReactionMs;
                }
                if (idleRubbing) {
                    const int motion = abs(dx) >= abs(dy) ? dx : dy;
                    const float target = std::clamp(motion * 2.f, -18.f, 18.f);
                    idleRubTilt = .3f * idleRubTilt + .7f * target;
                    if (!alwaysOn) {
                        const float dt = std::max(12u, now - idleFingerAt);
                        idleVelocityX = .20f * idleVelocityX + .80f * dx / dt;
                        idleVelocityY = .20f * idleVelocityY + .80f * dy / dt;
                        idleDragX = touch.x + idleGrabX;
                        idleDragY = touch.y + idleGrabY;
                        constrainIdlePosition(idleDragX, idleDragY);
                    }
                }
                idleFingerX = touch.x;
                idleFingerY = touch.y;
                idleFingerAt = now;
            }
        } else if (idleTouchCaptured && touch.wasReleased()) {
            if (idleRubbing && !alwaysOn) {
                const float speed = std::hypot(idleVelocityX, idleVelocityY);
                const bool flick = speed >= .85f && now - idleFingerAt <= 110;
                idleTravel = flick ? IdleTravel::Throw : IdleTravel::Return;
                idleTravelAt = now;
                if (flick) {
                    idleVelocityX /= speed;
                    idleVelocityY /= speed;
                    // Stop the flight at the first edge along the direction of the flick.
                    const float x = (idleDragX - center) / idlePlayfieldX;
                    const float y = (idleDragY - idlePlayfieldCenterY) / idlePlayfieldY;
                    const float vx = idleVelocityX / idlePlayfieldX;
                    const float vy = idleVelocityY / idlePlayfieldY;
                    const float a = vx * vx + vy * vy;
                    const float b = x * vx + y * vy;
                    const float c = std::min(0.f, x * x + y * y - 1.f);
                    idleFlightDistance = std::clamp((sqrtf(b * b - a * c) - b) / a, 0.f, 240.f);
                    ++idleThrows;
                }
            } else if (!idleRubbing && tap) {
                const unsigned previous = unsigned(idleReaction);
                idleReaction = static_cast<IdleReaction>(idleTaps
                    ? (previous + 1 + esp_random() % 2) % 3
                    : esp_random() % 3);
                idleReactionAt = now;
                ++idleTaps;
            }
            idleTouchCaptured = idleRubbing = false;
            lastFrame = 0;
        }
        if (!usedBySettings && !largeVoice && replyVisible() && touch.wasClicked() &&
            touch.base_y >= 0 && touch.base_y < replyTop) {
            if (transcriptOpen) transcriptOpen = false;
            else if (state == State::Reply) show(State::Idle);
            else replyDismissed = true;
            lastFrame = 0;
        }
        if (!usedBySettings && largeVoice && state == State::Reply && onVoiceButton) {
            transcriptOpen = true;
            scroll = 0;
            lastFrame = 0;
        }
        if (!usedBySettings && !largeVoice && replyVisible()) {
            const bool onText = touch.base_y >= replyTop && touch.base_y < 438;
            if (touch.wasPressed()) {
                replyDrag = ReplyDrag::None;
                touchY = touch.y;
            }
            if (onText && touch.isPressed() && touchY >= 0) {
                const int dx = abs(touch.distanceX()), dy = abs(touch.distanceY());
                if (replyDrag == ReplyDrag::None && std::max(dx, dy) >= 19)
                    replyDrag = dx > dy ? ReplyDrag::Horizontal : ReplyDrag::Vertical;
                if (replyDrag == ReplyDrag::Vertical && abs(touchY - touch.y) >= 19) {
                    scroll = std::max(0, scroll + (touchY > touch.y ? 1 : -1));
                    touchY = touch.y;
                    lastFrame = 0;
                }
            }
            if (touch.wasReleased()) {
                const int dx = touch.distanceX();
                if (onText && replyDrag != ReplyDrag::Vertical && abs(dx) >= 45 &&
                    abs(dx) > abs(touch.distanceY())) {
                    turnReplyPage(dx > 0 ? 1 : -1);
                }
                touchY = -1;
            }
        } else touchY = -1;

        microphone();
        speaker();
        const uint32_t stateNow = millis();
        if (gatewayReady && canReceiveNotice() && stateNow - lastNoticePoll >= 1000) {
            lastNoticePoll = stateNow;
            socket.sendTXT("notice-ready");
        }
        if (noticeId.length() && !reply.length() && stateNow - since > 12000)
            fail(ANNA_TEXT("Message timed out", "新消息接收超时", "通知がタイムアウトしました"));
        if ((state == State::Dialing || state == State::Waiting) &&
            stateNow - since > (preparingReply ? 95000 : 43000))
            fail(ANNA_TEXT("Timed out. Try again.", "等待超时，请重试", "時間切れです。再試行してください"));
        if (state == State::Error && stateNow - since > 4500) show(State::Idle);
    }
    const bool idleScreen = !Provision::active &&
                            (state == State::Idle || state == State::Reply || state == State::Error);
    if (!screenAsleep && idleScreen && M5.Imu.isEnabled() &&
        now - lastMotionAt >= 20) {
        lastMotionAt = now;
        if (M5.Imu.update()) {
            const auto& sample = M5.Imu.getImuData();
            motion.update(now, sample.accel.x, sample.accel.y, sample.accel.z,
                          sample.gyro.x, sample.gyro.y, sample.gyro.z);
            ++motionSamples;
        }
    }
    const uint32_t screenNow = millis();
    if (!idleScreen) {
        lastScreenActivity = screenNow;
        if (screenAsleep) {
            M5.Display.wakeup();
            screenAsleep = false;
            lastFrame = 0;
        }
    } else if (alwaysOn && !settingsOpen && state == State::Reply &&
               screenNow - lastScreenActivity >= screenIdleMs) {
        show(State::Idle);
        lastFrame = 0;
    } else if (!alwaysOn && !screenAsleep && screenNow - lastScreenActivity >= screenIdleMs) {
        M5.Display.sleep();
        screenAsleep = true;
    }
    const bool staticReply = playing && !settingsOpen && !largeVoiceView() && replyVisible() &&
                             scroll == drawnReplyScroll;
    const uint32_t frameInterval = Provision::active ? 320 : settingsOpen && !activeSlider ? 180 : staticReply ? 250 :
                                   inCall && playing ? 130 :
                                   recording || playing ? 85 : 42;
    if (!screenAsleep && now - lastFrame > frameInterval) {
        lastFrame = now;
        draw();
    }
    delay(1);
}

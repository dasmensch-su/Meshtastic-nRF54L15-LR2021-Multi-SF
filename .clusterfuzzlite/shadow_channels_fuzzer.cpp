// Fuzzer for ShadowChannels::shadowChannelsTryDecrypt.
//
// ShadowChannels is the Multi-SF bridges preset-agnostic decryption path
// (see src/mesh/ShadowChannels.{h,cpp}). For every received encrypted
// MeshPacket whose channel-hash byte does NOT match any of the nodes
// configured channels[], this function iterates a static table of
// known preset display names ("LongFast", "ShortSlow", ..., "LongMod"),
// stages the default-PSK AES key into the CryptoEngine, AES-CTR-decrypts
// the encrypted payload in place into a scratch buffer, and runs
// pb_decode_from_bytes() to see whether the plaintext parses as a valid
// meshtastic_Data protobuf.
//
// The function is attacker-reachable: any radio-layer packet the bridge
// hears whose wire hash doesnt match one of its configured channels
// falls through here. The input surface is therefore:
//
//   - p->channel        (8-bit hash byte; drives which preset(s) match)
//   - p->from, p->id    (used as the AES-CTR nonce via CryptoEngine)
//   - p->encrypted.bytes (up to sizeof(p->encrypted.bytes) of cipher)
//   - p->encrypted.size  (0..sizeof(bytes); decrypt + pb_decode over it)
//
// The harness builds a meshtastic_MeshPacket from the fuzzer bytes
// (first 9 bytes drive channel/from/id/size, remainder drives cipher)
// and feeds it to shadowChannelsTryDecrypt. It reuses the full-stack
// portduino boot from router_fuzzer.cpp so that the `crypto` global and
// `nodeSFTracker` are properly initialised (shadowChannelsTryDecrypt
// writes into nodeSFTracker on success via setHash()).

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "PortduinoGPIO.h"
#include "PortduinoGlue.h"
#include "PowerFSM.h"
#include "mesh/MeshTypes.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "mesh/TypeConversions.h"
#include "mesh/mesh-pb-constants.h"

// Only declared when MESHTASTIC_MULTI_SF_BRIDGE=1 is in the build flags.
// The ClusterFuzzLite pre-script appends this define unconditionally for
// the fuzzer build; gate the body here so the TU still compiles if the
// flag happens to be off (it becomes a no-op fuzzer that rejects every
// input).
#if defined(MESHTASTIC_MULTI_SF_BRIDGE) && MESHTASTIC_MULTI_SF_BRIDGE
#include "mesh/ShadowChannels.h"
#endif

namespace
{
constexpr uint32_t nodeId = 0x12345678;
bool hasBeenConfigured = false;

bool fuzzerRunning = false;
bool loopCanRun = true;
bool loopIsWaiting = false;
bool loopShouldExit = false;
std::mutex loopLock;
std::condition_variable loopCV;
std::thread meshtasticThread;

class ShouldExitException : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

void runLoopOnce()
{
    realHardware = true;
    std::unique_lock<std::mutex> lck(loopLock);
    fuzzerRunning = true;
    loopCanRun = true;
    loopCV.notify_one();
    loopCV.wait(lck, [] { return !loopCanRun && loopIsWaiting; });
}
} // namespace

bool loopCanSleep()
{
    std::unique_lock<std::mutex> lck(loopLock);
    loopIsWaiting = true;
    loopCV.notify_one();
    loopCV.wait(lck, [] { return loopCanRun || loopShouldExit; });
    loopIsWaiting = false;
    if (loopShouldExit)
        throw ShouldExitException("exit");
    if (!fuzzerRunning)
        return true;
    loopCanRun = false;
    return false;
}

void lateInitVariant()
{
    portduino_config.logoutputlevel = level_error;
    // Pre-install a primary channel with the default PSK so the stock
    // decrypt fall-through doesnt short-circuit shadowChannelsTryDecrypt
    // for hashes that happen to match our channel. We want the fuzzer to
    // exercise the shadow table specifically.
    channelFile.channels[0] = meshtastic_Channel{
        .has_settings = true,
        .settings =
            meshtastic_ChannelSettings{
                .psk = {.size = 1, .bytes = {/*defaultpskIndex=*/1}},
                .name = "LongFast",
                .uplink_enabled = true,
                .has_module_settings = true,
                .module_settings = {.position_precision = 16},
            },
        .role = meshtastic_Channel_Role_PRIMARY,
    };
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    hasBeenConfigured = true;
}

extern "C" {
int portduino_main(int argc, char **argv);

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    portduino_config.maxtophone = 5;

    meshtasticThread = std::thread([program = *argv[0]]() {
        char nodeIdStr[12];
        strcpy(nodeIdStr, std::to_string(nodeId).c_str());
        int argc = 7;
        char *argv[] = {program, "-d", "/tmp/meshtastic", "-h", nodeIdStr, "-p", "0", nullptr};
        try {
            portduino_main(argc, argv);
        } catch (const ShouldExitException &) {
        }
    });
    std::atexit([] {
        {
            const std::lock_guard<std::mutex> lck(loopLock);
            loopShouldExit = true;
            loopCV.notify_one();
        }
        meshtasticThread.join();
    });

    for (int i = 1; i < 20; ++i) {
        if (powerFSM.getState() == &stateON) {
            assert(hasBeenConfigured);
            assert(router);
            assert(nodeDB);
            assert(crypto);
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t length)
{
#if !(defined(MESHTASTIC_MULTI_SF_BRIDGE) && MESHTASTIC_MULTI_SF_BRIDGE)
    (void)data; (void)length;
    return -1; // Build without Multi-SF; reject every input.
#else
    // Wire layout the fuzzer consumes from `data`:
    //   [0]       channel hash byte (drives shadow-table iteration)
    //   [1..4]    from      (little-endian uint32; AES-CTR nonce half)
    //   [5..8]    id        (little-endian uint32; AES-CTR nonce half)
    //   [9..N-1]  encrypted payload (cap at MeshPackets ciphertext size)
    //
    // We need at least the 9-byte header; anything shorter cant drive
    // the AES-CTR nonce meaningfully. Reject to keep the corpus lean.
    if (length < 9)
        return -1;

    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_default;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p.channel = data[0];
    p.from = (uint32_t)data[1] | ((uint32_t)data[2] << 8) |
             ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 24);
    p.id = (uint32_t)data[5] | ((uint32_t)data[6] << 8) |
           ((uint32_t)data[7] << 16) | ((uint32_t)data[8] << 24);

    size_t cipherMax = sizeof(p.encrypted.bytes);
    size_t cipherLen = length - 9;
    if (cipherLen > cipherMax) cipherLen = cipherMax;
    memcpy(p.encrypted.bytes, data + 9, cipherLen);
    p.encrypted.size = (pb_size_t)cipherLen;

    // The function is idempotent on failure (returns false, leaves p
    // unchanged) and safe to call repeatedly; one pass per input.
    (void)shadowChannelsTryDecrypt(&p);

    // Give the scheduler one tick so any follow-on state (nodeSFTracker
    // setHash, any module wantsPacket side-effects if decode succeeded)
    // has a chance to surface as a crash in the fuzzers window.
    runLoopOnce();
    return 0;
#endif
}
}

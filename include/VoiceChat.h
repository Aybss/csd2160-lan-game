#pragma once
#include "Common.h"
#include <SFML/Audio.hpp>
// vcpkg may install opus header in either location depending on triplet
#if __has_include(<opus/opus.h>)
  #include <opus/opus.h>
#elif __has_include(<opus.h>)
  #include <opus.h>
#else
  #error "Cannot find opus.h - run: vcpkg install opus:x64-windows"
#endif
#include <vector>
#include <deque>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>

//  Outbound: captures mic, compresses with Opus, calls onFrame callback 
class VoiceCapture : public sf::SoundRecorder
{
public:
    using FrameCallback = std::function<void(const uint8_t* data, int len)>;

    VoiceCapture();
    ~VoiceCapture();

    bool start(FrameCallback cb);
    void stop();

protected:
    bool onStart() override;
    bool onProcessSamples(const int16_t* samples, std::size_t count) override;
    void onStop() override;

private:
    OpusEncoder*  m_enc    = nullptr;
    FrameCallback m_cb;
    std::vector<int16_t> m_buf;   // accumulate samples until full frame
    int m_frameSamples = 0;       // VOICE_SAMPLE_RATE * VOICE_FRAME_MS / 1000
};

//  Inbound: receives compressed frames, decodes and plays per-player 
class VoicePlayer
{
public:
    VoicePlayer();
    ~VoicePlayer();

    // Feed a compressed Opus frame for this player
    void feed(const uint8_t* data, int len);
    // Call every frame to play queued audio
    void tick();

private:
    OpusDecoder* m_dec = nullptr;

    // Jitter buffer: small queue of decoded PCM chunks
    std::deque<std::vector<int16_t>> m_jitter;
    static constexpr int JITTER_MAX = 4;

    sf::SoundBuffer              m_buf;
    std::optional<sf::Sound>     m_sound;  // SFML 3: constructed after buffer loaded
    bool            m_playing = false;
};

//  Manager: owns one VoiceCapture + one VoicePlayer per remote player 
class VoiceChat
{
public:
    VoiceChat();

    // Call with send callback (sends PktVoiceData over network)
    using SendFn = std::function<void(const void* data, int len)>;
    void init(uint8_t myPid, SendFn sendFn);

    // Push-to-talk: call every frame with key state
    void setTalking(bool talking);
    bool isTalking() const { return m_talking; }

    // Feed incoming compressed audio from another player
    void feedIncoming(uint8_t pid, const uint8_t* data, int len);

    // Call every frame
    void tick();

    bool available() const { return m_available; }

private:
    uint8_t  m_myPid   = 0xFF;
    bool     m_talking  = false;
    bool     m_available = false;
    SendFn   m_sendFn;

    VoiceCapture m_capture;
    std::array<VoicePlayer*, MAX_PLAYERS> m_players{};
};

#include "VoiceChat.h"
#include <cstring>
#include <iostream>
#include <algorithm>

static constexpr int FRAME_SAMPLES = VOICE_SAMPLE_RATE * VOICE_FRAME_MS / 1000; // 320

// VoiceCapture
VoiceCapture::VoiceCapture()
{
    m_frameSamples = FRAME_SAMPLES;
    int err = 0;
    m_enc = opus_encoder_create(VOICE_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &err);
    if(err != OPUS_OK || !m_enc){
        std::cerr << "[Voice] Opus encoder init failed: " << opus_strerror(err) << "\n";
        m_enc = nullptr;
    } else {
        opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(24000));  // 24kbps
        opus_encoder_ctl(m_enc, OPUS_SET_DTX(1));           // discontinuous tx
    }
}

VoiceCapture::~VoiceCapture()
{
    if(m_enc) opus_encoder_destroy(m_enc);
}

bool VoiceCapture::start(FrameCallback cb)
{
    if(!m_enc) return false;
    m_cb = cb;
    m_buf.clear();
    // SFML 3: setChannelCount before start; sample rate passed to start()
    setChannelCount(1);
    return SoundRecorder::start(VOICE_SAMPLE_RATE);
}

void VoiceCapture::stop()
{
    SoundRecorder::stop();
}

bool VoiceCapture::onStart()
{
    m_buf.clear();
    return true;
}

bool VoiceCapture::onProcessSamples(const int16_t* samples, std::size_t count)
{
    if(!m_enc || !m_cb) return true;

    // Accumulate samples
    m_buf.insert(m_buf.end(), samples, samples + count);

    // Encode full frames
    while((int)m_buf.size() >= m_frameSamples)
    {
        uint8_t out[VOICE_MAX_BYTES];
        int bytes = opus_encode(m_enc, m_buf.data(), m_frameSamples, out, VOICE_MAX_BYTES);
        if(bytes > 0) m_cb(out, bytes);
        m_buf.erase(m_buf.begin(), m_buf.begin() + m_frameSamples);
    }
    return true;
}

void VoiceCapture::onStop() { m_buf.clear(); }

// VoicePlayer
VoicePlayer::VoicePlayer()
{
    int err = 0;
    m_dec = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &err);
    if(err != OPUS_OK || !m_dec){
        std::cerr << "[Voice] Opus decoder init failed\n";
        m_dec = nullptr;
    }
}

VoicePlayer::~VoicePlayer()
{
    if(m_dec) opus_decoder_destroy(m_dec);
}

void VoicePlayer::feed(const uint8_t* data, int len)
{
    if(!m_dec) return;
    if((int)m_jitter.size() >= JITTER_MAX) m_jitter.pop_front(); // drop oldest

    std::vector<int16_t> pcm(FRAME_SAMPLES);
    int decoded = opus_decode(m_dec, data, len, pcm.data(), FRAME_SAMPLES, 0);
    if(decoded > 0){
        pcm.resize(decoded);
        m_jitter.push_back(std::move(pcm));
    }
}

void VoicePlayer::tick()
{
    if(!m_dec || m_jitter.empty()) return;

    // Only push new audio when sound is not playing
    if(m_sound && m_sound->getStatus() == sf::Sound::Status::Playing) return;

    // Drain one frame
    auto& frame = m_jitter.front();
    // SFML 3: SoundBuffer::loadFromSamples(samples, sampleCount, channelCount, sampleRate)
    // SFML 3: loadFromSamples needs channel map as 5th arg
    if(m_buf.loadFromSamples(frame.data(), frame.size(), 1, VOICE_SAMPLE_RATE,
                             {sf::SoundChannel::Mono})){
        m_sound.emplace(m_buf);  // reconstruct with new buffer data
        m_sound->play();
    }
    m_jitter.pop_front();
}

// VoiceChat
VoiceChat::VoiceChat()
{
    for(auto& p : m_players) p = nullptr;
}

void VoiceChat::init(uint8_t myPid, SendFn sendFn)
{
    m_myPid  = myPid;
    m_sendFn = sendFn;

    // Create a player slot for each possible remote player
    for(int i=0;i<MAX_PLAYERS;i++){
        if(i != myPid) m_players[i] = new VoicePlayer();
    }

    // Start capture with push-to-talk gated callback
    bool ok = m_capture.start([this](const uint8_t* data, int len){
        if(!m_talking) return;
        // Build packet and send
        PktVoiceData pkt;
        pkt.pid    = m_myPid;
        pkt.length = (uint16_t)std::min(len, (int)VOICE_MAX_BYTES);
        memcpy(pkt.data, data, pkt.length);
        // Only send the header + actual data bytes (not full VOICE_MAX_BYTES)
        int pktSize = (int)(sizeof(PktType) + sizeof(uint8_t) + sizeof(uint16_t) + pkt.length);
        m_sendFn(&pkt, pktSize);
    });

    m_available = ok;
    if(!ok) std::cerr << "[Voice] Microphone not available - voice chat disabled\n";
    else    std::cout  << "[Voice] Voice chat ready. Hold V to talk.\n";
}

void VoiceChat::setTalking(bool talking)
{
    m_talking = talking && m_available;
}

void VoiceChat::feedIncoming(uint8_t pid, const uint8_t* data, int len)
{
    if(pid >= MAX_PLAYERS || pid == m_myPid) return;
    if(m_players[pid]) m_players[pid]->feed(data, len);
}

void VoiceChat::tick()
{
    for(int i=0;i<MAX_PLAYERS;i++)
        if(m_players[i]) m_players[i]->tick();
}

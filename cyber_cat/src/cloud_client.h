#pragma once
#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <freertos/FreeRTOS.h>

enum CloudState {
    CLOUD_IDLE,
    CLOUD_BUSY,
    CLOUD_STT_DONE,   // STT finished, transcript ready, LLM in progress
    CLOUD_DONE,       // LLM reply ready — caller should start TTS
    CLOUD_TTS_DONE,   // TTS downloaded + played to completion
    CLOUD_ERROR
};

class CloudClient {
public:
    // apiKey / apiBase / model — for LLM chat completions
    // sttApiKey / sttApiBase  — for Whisper STT (can be same key, different base)
    void begin(const char *apiKey,    const char *apiBase,    const char *model,
               const char *sttApiKey, const char *sttApiBase,
               const char *ttsApiKey, const char *ttsApiBase,
               const char *ttsModel,  const char *ttsVoice);

    // Send plain text message directly to LLM
    bool sendChat(const char *message);

    // Upload PCM audio → STT → feed transcript to LLM automatically
    bool sendAudio(const uint8_t *pcm, size_t bytes);

    // Download TTS audio for aiText and play it via ES8311 (async, Core 1).
    // Poll for CLOUD_TTS_DONE / CLOUD_ERROR.
    bool sendTTS(const char *aiText);

    CloudState  poll()          const;
    const char *getResponse()   const;
    const char *getError()      const;
    const char *getTranscript() const;
    const char *getEmotion()    const;  // "neutral"|"happy"|"thinking"|"talking"

    void consume();

    // Abandon the in-flight request. A running worker cannot be interrupted
    // mid-HTTP, so it keeps running but its result is discarded and the state
    // returns to CLOUD_IDLE when it finishes; TTS playback stops immediately.
    // New send*() calls are rejected until then (see isBusy()).
    void cancel();
    bool isBusy() const;

    // Backend memory session: "<MAC>" by default, "<prefix>-<MAC>" when set
    // (keeps test traffic out of the cat's real conversation history).
    void setSessionPrefix(const char *prefix);

private:
    void _finish(CloudState s);
    void _setProgress(CloudState s);

    static void _chatWorkerTask(void *param);
    static void _sttWorkerTask(void *param);
    static void _ttsWorkerTask(void *param);

    bool _doStt();
    bool _doChat(const char *text);
    bool _doTts(const char *text);
    bool _doBackendChat();          // Phase 5: unified POST /chat → text+emotion+audio_url
    bool _downloadAndPlayWav(const char *url);  // Phase 5: GET audio_url → es8311Play

    const char *_apiKey     = nullptr;
    const char *_apiBase    = nullptr;
    const char *_model      = nullptr;
    const char *_sttApiKey  = nullptr;
    const char *_sttApiBase = nullptr;
    const char *_ttsApiKey  = nullptr;
    const char *_ttsApiBase = nullptr;
    const char *_ttsModel   = nullptr;
    const char *_ttsVoice   = nullptr;

    std::atomic<CloudState> _state{CLOUD_IDLE};
    std::atomic<bool>       _cancelled{false};
    portMUX_TYPE            _mux = portMUX_INITIALIZER_UNLOCKED;

    char    *_response    = nullptr;
    char    *_errMsg      = nullptr;
    char    *_transcript  = nullptr;
    char    *_pendingMsg  = nullptr;
    char     _emotion[32]  = {};   // Phase 5: emotion tag from backend
    char     _audioUrl[256] = {};  // Phase 5: TTS audio URL from backend
    char     _sessionPrefix[16] = {};

    uint8_t *_pendingPcm   = nullptr;
    size_t   _pendingBytes = 0;

    static const size_t RESP_BUF_SIZE   = 1024;
    static const size_t ERR_BUF_SIZE    = 256;
    static const size_t TTS_MAX_BYTES   = 500000;  // max TTS WAV size (~15s @16kHz mono)
    static const int    TTS_WORKER_STACK = 28672;  // 28KB: TLS + HTTP + conversion buf
};

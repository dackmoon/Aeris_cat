#pragma once
#include <stddef.h>
#include <atomic>

enum CloudState {
    CLOUD_IDLE,
    CLOUD_BUSY,
    CLOUD_DONE,
    CLOUD_ERROR
};

class CloudClient {
public:
    void begin(const char *apiKey, const char *apiBase, const char *model);

    bool sendChat(const char *message);

    CloudState poll();
    const char *getResponse() const;
    const char *getError() const;

    void consume();

private:
    static void _workerTask(void *param);

    const char      *_apiKey   = nullptr;
    const char      *_apiBase  = nullptr;
    const char      *_model    = nullptr;
    std::atomic<CloudState> _state{CLOUD_IDLE};

    char *_response = nullptr;
    char *_errMsg   = nullptr;
    char *_pendingMsg = nullptr;

    static const size_t RESP_BUF_SIZE = 1024;
};

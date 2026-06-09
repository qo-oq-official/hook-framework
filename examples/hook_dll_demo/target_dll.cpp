#include "target_dll.h"
#include <cstring>
#include <cmath>

extern "C" {

TDLL_API __declspec(noinline) int math_pow(int base, int exp) {
    int result = 1;
    for (int i = 0; i < exp; ++i) result *= base;
    return result;
}

TDLL_API __declspec(noinline) double math_lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

TDLL_API __declspec(noinline) size_t string_count(const char* str, char ch) {
    if (!str) return 0;
    size_t count = 0;
    for (const char* p = str; *p; ++p) {
        if (*p == ch) count++;
    }
    return count;
}

}

__declspec(noinline) uint32_t CryptoEngine::encrypt(const uint8_t* input, size_t len, uint8_t* output) {
    if (!input || !output || len == 0) return 0;
    uint32_t checksum = 0;
    for (size_t i = 0; i < len; ++i) {
        output[i] = input[i] ^ static_cast<uint8_t>(key_ >> ((i % 4) * 8));
        checksum += output[i];
    }
    return checksum;
}

__declspec(noinline) uint32_t CryptoEngine::decrypt(const uint8_t* input, size_t len, uint8_t* output) {
    return encrypt(input, len, output);
}

__declspec(noinline) void CryptoEngine::setKey(uint32_t key) {
    key_ = key;
}

namespace protocol {

TDLL_API __declspec(noinline) int encode_frame(const uint8_t* payload, size_t len,
                                                uint8_t* frame, size_t frameCap) {
    if (!payload || !frame) return -1;
    size_t needed = len + 4;
    if (frameCap < needed) return -2;

    frame[0] = 0xAA;
    frame[1] = 0x55;
    memcpy(frame + 2, payload, len);
    frame[len + 2] = 0x55;
    frame[len + 3] = 0xAA;
    return static_cast<int>(needed);
}

TDLL_API __declspec(noinline) int decode_frame(const uint8_t* frame, size_t frameLen,
                                                uint8_t* payload, size_t payloadCap) {
    if (!frame || !payload) return -1;
    if (frameLen < 4) return -2;
    if (frame[0] != 0xAA || frame[1] != 0x55) return -3;
    if (frame[frameLen - 2] != 0x55 || frame[frameLen - 1] != 0xAA) return -3;

    size_t dataLen = frameLen - 4;
    if (payloadCap < dataLen) return -4;
    memcpy(payload, frame + 2, dataLen);
    return static_cast<int>(dataLen);
}

}

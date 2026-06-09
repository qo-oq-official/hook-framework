#pragma once
#include <cstdint>
#include <cstddef>

#ifdef TARGET_DLL_EXPORTS
#define TDLL_API __declspec(dllexport)
#else
#define TDLL_API __declspec(dllimport)
#endif

extern "C" {
    TDLL_API int math_pow(int base, int exp);
    TDLL_API double math_lerp(double a, double b, double t);
    TDLL_API size_t string_count(const char* str, char ch);
}

class TDLL_API CryptoEngine {
public:
    uint32_t encrypt(const uint8_t* input, size_t len, uint8_t* output);
    uint32_t decrypt(const uint8_t* input, size_t len, uint8_t* output);
    void setKey(uint32_t key);
private:
    uint32_t key_ = 0x12345678;
};

namespace protocol {
    TDLL_API int encode_frame(const uint8_t* payload, size_t len, uint8_t* frame, size_t frameCap);
    TDLL_API int decode_frame(const uint8_t* frame, size_t frameLen, uint8_t* payload, size_t payloadCap);
}

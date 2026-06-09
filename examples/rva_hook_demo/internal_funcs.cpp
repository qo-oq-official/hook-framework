#include "internal_funcs.h"
#include <cstring>
#include <cmath>

// ============================================================
// 非导出函数实现
// ============================================================

int secret_compute(int a, int b, int mode) {
    switch (mode) {
        case 0: return a + b;
        case 1: return a * b;
        case 2: return a ^ b;
        case 3: return (a << 4) | (b & 0xF);
        default: return a - b;
    }
}

double physics_gravity(double mass, double height) {
    const double g = 9.80665;
    return mass * g * height;
}

bool auth_check(const char* user, const char* token) {
    if (!user || !token) return false;
    size_t ulen = strlen(user);
    size_t tlen = strlen(token);
    return (ulen > 0 && tlen >= 8);
}

// ============================================================
// C++ 类方法实现
// ============================================================

int NetworkSession::sendPacket(const uint8_t* data, size_t len, uint32_t flags) {
    if (!data || len == 0) return -1;
    if (!authenticated_ && !(flags & 0x01)) return -2;
    bytesSent_ += static_cast<int>(len);
    return static_cast<int>(len);
}

int NetworkSession::recvPacket(uint8_t* buf, size_t bufSize) {
    if (!buf || bufSize == 0) return -1;
    size_t recvLen = bufSize > 64 ? 64 : bufSize;
    memset(buf, 0xAA, recvLen);
    bytesRecv_ += static_cast<int>(recvLen);
    return static_cast<int>(recvLen);
}

bool NetworkSession::authenticate(const std::string& user, const std::string& pass) {
    if (user.empty() || pass.empty()) return false;
    authenticated_ = (user.length() >= 3 && pass.length() >= 6);
    return authenticated_;
}

uint32_t NetworkSession::checksum(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = (sum << 1) ^ data[i];
    }
    return sum;
}

// ============================================================
// 命名空间函数实现
// ============================================================

namespace engine {
namespace render {

static uint32_t g_lastColor = 0;
static int g_drawCount = 0;

void drawRect(int x, int y, int w, int h, uint32_t color) {
    g_lastColor = color;
    g_drawCount++;
}

int blendColors(uint32_t src, uint32_t dst, float alpha) {
    auto blend = [](uint8_t s, uint8_t d, float a) -> uint8_t {
        return static_cast<uint8_t>(s * a + d * (1.0f - a));
    };
    uint8_t r = blend((src >> 16) & 0xFF, (dst >> 16) & 0xFF, alpha);
    uint8_t g = blend((src >> 8) & 0xFF, (dst >> 8) & 0xFF, alpha);
    uint8_t b = blend(src & 0xFF, dst & 0xFF, alpha);
    return (r << 16) | (g << 8) | b;
}

}
}

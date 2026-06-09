#pragma once
#include <cstdint>
#include <string>

// ============================================================
// 非导出的普通函数 (静态链接, 无dllexport)
// ============================================================

__declspec(noinline) int secret_compute(int a, int b, int mode);
__declspec(noinline) double physics_gravity(double mass, double height);
__declspec(noinline) bool auth_check(const char* user, const char* token);

// ============================================================
// C++ 类 (成员函数有name mangling)
// ============================================================

class NetworkSession {
public:
    __declspec(noinline) int sendPacket(const uint8_t* data, size_t len, uint32_t flags);
    __declspec(noinline) int recvPacket(uint8_t* buf, size_t bufSize);
    __declspec(noinline) bool authenticate(const std::string& user, const std::string& pass);

    static __declspec(noinline) uint32_t checksum(const uint8_t* data, size_t len);

    int getBytesSent() const { return bytesSent_; }
    int getBytesRecv() const { return bytesRecv_; }

private:
    int bytesSent_ = 0;
    int bytesRecv_ = 0;
    bool authenticated_ = false;
};

// ============================================================
// 命名空间内部函数
// ============================================================

namespace engine {
namespace render {

__declspec(noinline) void drawRect(int x, int y, int w, int h, uint32_t color);
__declspec(noinline) int blendColors(uint32_t src, uint32_t dst, float alpha);

}
}

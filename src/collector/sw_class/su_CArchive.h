// su_CArchive — swccu.dll, 0x70 bytes (operator new(0x70u))
//
// Confirmed offsets:
//   +0x18: CStringT (ATL::CStringT<wchar_t>) — 8 bytes internal + name
//   +0x20: m_nMode  (DWORD — bit0 = storing, from MapObject)
//   +0x24: m_writeFlag (DWORD = 1)
//   +0x30: m_pFile  (CFile*)
//   +0x38: m_lpBufCur
//   +0x40: m_lpBufEnd
//   +0x48: m_lpBufStart / external object
//   +0x50: m_nIndexCounter ★ (ReadClass/WriteClass/AddToLoadArrayDirect)
//   +0x58: m_pLoadStoreMap ★★ (su_CPtrArray* | su_CMapPtrToPtr*)
//   +0x60: m_pRemapTable (legacy schema remap — su_CMapPtrToPtr*)
//   +0x68: m_nBufSize
//   +0x6C: m_nHashGrowBy
//
// Fields before +0x18 are SW-internal (state flags, etc.) and not
// accessed by our code. Exact layout preserved for size conformance.

#pragma once
#include <cstdint>
#include <cstddef>

struct su_CArchive {
    // ── +0x00 ~ +0x17: internal state ──
    uint8_t   m_internal_0x00[0x18];

    // ── +0x18: CStringT<wchar_t> — 8 bytes ──
    void*     m_pStreamNameData;       // +0x18: CStringT internal pointer

    // ── +0x20 ~ +0x2F: mode flags ──
    uint32_t  m_nMode;                 // +0x20: bit0 = storing (MapObject line 8937)
    uint32_t  m_writeFlag;             // +0x24: = 1 (constructor line 3197)
    uint8_t   m_internal_0x28[8];

    // ── +0x30 ~ +0x4F: CFile / buffer ──
    void*     m_pFile;                 // +0x30: CFile*
    uint8_t*  m_lpBufCur;              // +0x38
    uint8_t*  m_lpBufEnd;              // +0x40
    void*     m_extObj;                // +0x48: external object or buffer base

    // ── +0x50 ~ +0x5F: class/object index cache ──
    uint32_t  m_nIndexCounter;         // +0x50 ★ counter = next available index
    uint32_t  pad_54;
    void*     m_pLoadStoreMap;         // +0x58 ★★ su_CPtrArray* (load side) or su_CMapPtrToPtr* (store side)

    // ── +0x60 ~ +0x67: legacy remap ──
    void*     m_pRemapTable;           // +0x60: su_CMapPtrToPtr* (ReadClass line 66-76)

    // ── +0x68 ~ +0x6F: buffer/hash params ──
    uint32_t  m_nBufSize;              // +0x68
    uint32_t  m_nHashGrowBy;           // +0x6C
};
static_assert(sizeof(su_CArchive) == 0x70, "su_CArchive must be 112 bytes");
static_assert(offsetof(su_CArchive, m_nIndexCounter) == 0x50, "counter at +0x50");
static_assert(offsetof(su_CArchive, m_pLoadStoreMap) == 0x58, "map at +0x58");

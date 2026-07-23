#pragma once
#include <Windows.h>
struct CRuntimeClass {
    LPCSTR m_lpszClassName;        // 类名字符串指针
    int    m_nObjectSize;           // 对象sizeof大小
    UINT   m_wSchema;               // 序列化Schema号
};

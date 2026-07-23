#pragma once
struct CObjectVptr {
    void* dtor;
    void* GetRuntimeClass;
    void* Serialize;
    void* Fun;
    void* Serialize1;
};
struct CObject {
    CObjectVptr* vptr;
};

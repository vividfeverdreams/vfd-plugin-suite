#pragma once
// Non-owning view of the host's live queues, valid only during this process call.
// Stream points directly: no fixed event capacity, truncation or audio allocation.
namespace energyBridge {
struct Source {
    void* owner{}; void* changes{}; const void* bypassParameter{}; bool initialBypass{};
    int (*queueCount)(void*){};
    const void* (*parameter)(void*,void*,int){};
    int (*pointCount)(void*,int){};
    bool (*point)(void*,int,int,int&,float&){};
    bool active() const noexcept { return owner!=nullptr; }
};
inline thread_local Source current;
}

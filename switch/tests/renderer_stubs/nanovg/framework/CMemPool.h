#pragma once
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

constexpr uint32_t DK_CMDMEM_ALIGNMENT = 4;
struct TestBlock { uint32_t size; bool destroyed{}; };
struct TestCommands;
using DkCmdBuf = TestCommands*;
using DkCmdList = uintptr_t;
inline unsigned pending_frames{}, fence_waits{};

namespace dk {
struct Fence {
    bool pending{};
    void wait() {
        ++fence_waits;
        if (pending) { --pending_frames; pending = false; }
    }
};
}

struct TestCommands {
    void* userdata{};
    void (*callback)(void*, DkCmdBuf, size_t){};
    uint32_t available{};
    size_t bytes{};
    std::vector<TestBlock*> blocks;

    bool write(uint32_t size) {
        if (size > available && callback) callback(userdata, this, size);
        if (size > available) return false;
        available -= size;
        bytes += size;
        return true;
    }
};

namespace dk {
struct CmdBuf {
    DkCmdBuf commands;
    CmdBuf(DkCmdBuf value) : commands(value) {}
    operator DkCmdBuf() const { return commands; }
    void clear() {
        assert(pending_frames == 0); // No reuse before the previous GPU frame ends.
        commands->blocks.clear(); commands->bytes = 0;
    }
    void addMemory(TestBlock* block, uint32_t offset, uint32_t size) {
        assert(!block->destroyed && offset == 0 && size <= block->size);
        for (auto* used : commands->blocks) assert(used != block);
        commands->blocks.push_back(block);
        commands->available = size;
    }
    void signalFence(Fence& fence) {
        assert(commands->write(64)); // Fence recording can itself exhaust memory.
        assert(!fence.pending);
        fence.pending = true; ++pending_frames;
    }
    DkCmdList finishList() { return commands->bytes; }
};
}

class CMemPool {
public:
    struct Handle {
        TestBlock* block{};
        operator bool() const { return block != nullptr; }
        uint32_t getSize() const { return block->size; }
        uint32_t getOffset() const { return 0; }
        TestBlock* getMemBlock() const { return block; }
        void destroy() {
            assert(pending_frames == 0 && !block->destroyed);
            block->destroyed = true; block = nullptr;
        }
    };
    std::vector<std::unique_ptr<TestBlock>> allocations;
    bool fail{};
    Handle allocate(uint32_t size, uint32_t alignment = DK_CMDMEM_ALIGNMENT) {
        if (fail) return {};
        assert(size && size % alignment == 0);
        allocations.push_back(std::make_unique<TestBlock>(TestBlock{size}));
        return {allocations.back().get()};
    }
};

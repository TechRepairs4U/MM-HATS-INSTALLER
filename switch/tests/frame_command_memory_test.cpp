#include "render/frame_command_memory.hpp"
#include <iostream>

using sphaira::render::FrameCommandMemory;

int main() {
    constexpr uint32_t old_limit = 0x20000;
    TestCommands fixed;
    fixed.available = old_limit;
    assert(fixed.write(old_limit));
    assert(!fixed.write(4)); // Reproduce exhaustion without an add-memory callback.

    CMemPool pool;
    {
        FrameCommandMemory memory;
        assert(memory.allocate(pool, old_limit));
        TestCommands commands;
        commands.userdata = &memory;
        commands.callback = FrameCommandMemory::AddMemory;
        size_t peak_allocations{};
        for (unsigned frame = 0; frame < 100; ++frame) {
            memory.begin(&commands);
            // Many uniform/draw records: over 8 MiB per frame, 64x the old limit.
            for (unsigned draw = 0; draw < 32768; ++draw) assert(commands.write(256));
            assert(memory.end(&commands) == 32768 * 256 + 64);
            assert(commands.blocks.size() == 65);
            if (!frame) peak_allocations = pool.allocations.size();
            assert(pool.allocations.size() == peak_allocations); // Reuse, not a per-frame leak.
            for (const auto& block : pool.allocations) assert(!block->destroyed);
        }

        memory.begin(&commands);
        assert(commands.write(old_limit * 2 + 1)); // Single oversized reservation, aligned up.
        memory.end(&commands);
        memory.begin(&commands);
        assert(commands.write(old_limit * 2 + 1));
        const auto large_frame_allocations = pool.allocations.size();
        memory.end(&commands);
        memory.begin(&commands);
        assert(commands.write(old_limit * 2 + 1));
        assert(pool.allocations.size() == large_frame_allocations);
        memory.end(&commands);

        memory.begin(&commands);
        pool.fail = true;
        const auto supplied = commands.blocks.size();
        FrameCommandMemory::AddMemory(&memory, &commands, old_limit * 4);
        assert(commands.blocks.size() == supplied);
        FrameCommandMemory::AddMemory(&memory, &commands, UINT32_MAX);
        assert(commands.blocks.size() == supplied);
        FrameCommandMemory::AddMemory(&memory, &commands, size_t{1} << 32);
        assert(commands.blocks.size() == supplied);
        pool.fail = false;
        memory.end(&commands);
    }
    assert(pending_frames == 0 && fence_waits >= 105);
    for (const auto& block : pool.allocations) assert(block->destroyed);
    std::cout << "Frame command memory: exhaustion, growth, fence rollover, reuse, alignment and cleanup passed\n";
}

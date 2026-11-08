#pragma once

#include <nanovg/framework/CMemPool.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace sphaira::render {

// Retain every command block until the frame's fence completes. A busy screen
// can need more than NanoVG's original 128 KiB, including while recording the
// final fence. Adding a block preserves the commands already recorded.
class FrameCommandMemory {
public:
    FrameCommandMemory() = default;
    FrameCommandMemory(const FrameCommandMemory&) = delete;
    FrameCommandMemory& operator=(const FrameCommandMemory&) = delete;

    ~FrameCommandMemory() {
        m_fence.wait();
        for (auto& block : m_blocks) block.destroy();
    }

    bool allocate(CMemPool& pool, uint32_t block_size) {
        m_pool = &pool;
        m_block_size = AlignSize(block_size);
        return m_block_size && AllocateBlock(m_block_size);
    }

    void begin(dk::CmdBuf commands) {
        m_fence.wait();
        commands.clear();
        m_next_block = 0;
        AddMemory(this, commands, m_block_size);
    }

    DkCmdList end(dk::CmdBuf commands) {
        commands.signalFence(m_fence);
        return commands.finishList();
    }

    static void AddMemory(void* userdata, DkCmdBuf raw_commands, size_t min_size) {
        auto& self = *static_cast<FrameCommandMemory*>(userdata);
        if (min_size > std::numeric_limits<uint32_t>::max()) return;
        const auto size = AlignSize(std::max(static_cast<uint32_t>(min_size), self.m_block_size));
        // Leave genuine allocation failure to deko3d's normal error handling.
        if (!self.m_pool || !size) return;

        // Never reuse a block already used by this frame. Oversized commands
        // may skip smaller blocks; those remain available on the next frame.
        while (self.m_next_block < self.m_blocks.size() &&
               self.m_blocks[self.m_next_block].getSize() < size) {
            ++self.m_next_block;
        }
        if (self.m_next_block == self.m_blocks.size() && !self.AllocateBlock(size)) return;

        const auto& block = self.m_blocks[self.m_next_block++];
        dk::CmdBuf commands{raw_commands};
        commands.addMemory(block.getMemBlock(), block.getOffset(), block.getSize());
    }

private:
    static uint32_t AlignSize(uint32_t size) {
        constexpr uint32_t padding = DK_CMDMEM_ALIGNMENT - 1;
        if (size > std::numeric_limits<uint32_t>::max() - padding) return 0;
        return (size + padding) & ~padding;
    }

    bool AllocateBlock(uint32_t size) {
        auto block = m_pool->allocate(size, DK_CMDMEM_ALIGNMENT);
        if (!block) return false;
        m_blocks.push_back(block);
        return true;
    }

    CMemPool* m_pool{};
    uint32_t m_block_size{};
    std::vector<CMemPool::Handle> m_blocks;
    size_t m_next_block{};
    dk::Fence m_fence{};
};

} // namespace sphaira::render

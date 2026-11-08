# Apply to both fresh and previously configured dependency checkouts. Fail if
# the pinned renderer changes, instead of silently shipping the old crash.
function(mm_replace_renderer_text path old new)
    file(READ "${path}" contents)
    string(FIND "${contents}" "${new}" already_patched)
    if (already_patched EQUAL -1)
        string(FIND "${contents}" "${old}" found)
        if (found EQUAL -1)
            message(FATAL_ERROR "NanoVG command memory patch does not match ${path}")
        endif()
        string(REPLACE "${old}" "${new}" contents "${contents}")
        file(WRITE "${path}" "${contents}")
    endif()
endfunction()

mm_replace_renderer_text("${nanovg_SOURCE_DIR}/include/nanovg/dk_renderer.hpp"
    "#include \"framework/CCmdMemRing.h\""
    "#include <render/frame_command_memory.hpp>")
mm_replace_renderer_text("${nanovg_SOURCE_DIR}/include/nanovg/dk_renderer.hpp"
    "CCmdMemRing<1> m_dyn_cmd_mem;"
    "sphaira::render::FrameCommandMemory m_dyn_cmd_mem;")
mm_replace_renderer_text("${nanovg_SOURCE_DIR}/source/dk_renderer.cpp"
    "m_dyn_cmd_buf = dk::CmdBufMaker{m_device}.create();"
    "m_dyn_cmd_buf = dk::CmdBufMaker{m_device}\n            .setUserData(&m_dyn_cmd_mem)\n            .setCbAddMem(sphaira::render::FrameCommandMemory::AddMemory)\n            .create();")
target_include_directories(nanovg PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

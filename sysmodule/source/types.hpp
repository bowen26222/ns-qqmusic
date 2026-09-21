#pragma once

#include <switch.h>

namespace qqmusic {

    enum class PlayerStatus : u8 {
        Playing,
        FetchNext,
    };

    enum class ShuffleMode : u8 {
        Off,
        On,
    };

    enum class RepeatMode : u8 {
        Off,
        One,
        All,
    };

    enum class EnqueueType : u8 {
        Front,
        Back,
    };

    // 与 ipc/client.h 的 CurrentStats 字段一致（u32×3）。
    struct CurrentStats {
        u32 sample_rate;
        u32 current_frame;
        u32 total_frames;
    };

}

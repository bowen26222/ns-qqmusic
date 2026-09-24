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

    struct CurrentStats {
        u32 sample_rate;
        u32 current_frame;
        u32 total_frames;
        u32 index;
    };

}

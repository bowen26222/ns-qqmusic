#pragma once

#include "../types.hpp"
#include <string>
#include <vector>

namespace qqmusic::impl {

    Result Initialize();
    void Exit();

    void TuneThreadFunc(void *);
    void GpioThreadFunc(void *);
    void PmdmntThreadFunc(void *);
    void SetTitleEnabled(u64 tid, bool enabled);
    void SetTitleEnabledDefault(bool enabled);
    bool GetStatus();
    bool GetAudioUnavailable();
    bool GetBlacklistPaused();
    bool GetLastPlayError();
    bool GetRadioMode();
    void SetRadioMode(bool enabled);
    void Play();
    void Pause();
    void Next();
    void Prev();

    float GetVolume();
    void SetVolume(float volume);
    float GetTitleVolume();
    void SetTitleVolume(float volume);
    float GetDefaultTitleVolume();
    void SetDefaultTitleVolume(float volume);

    RepeatMode GetRepeatMode();
    void SetRepeatMode(RepeatMode mode);
    ShuffleMode GetShuffleMode();
    void SetShuffleMode(ShuffleMode mode);

    u32 GetPlaylistSize();
    u32 GetPlaylistItem(u32 index, char* buffer, size_t buffer_size);
    Result GetCurrentQueueItem(CurrentStats *out, char* buffer, size_t buffer_size);
    void ClearQueue();
    void MoveQueueItem(u32 src, u32 dst);
    void Select(u32 index);
    void Seek(u32 position);

    Result Enqueue(const char* buffer, size_t buffer_length, EnqueueType type, bool save = true);
    void SaveQueue();
    Result Remove(u32 index);

}

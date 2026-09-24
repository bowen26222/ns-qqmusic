#!/usr/bin/env python3
"""Compile the production player.cpp against deterministic blocked I/O/audio fakes.
Run: python3 tools/player-concurrency-smoke.py
Optional: CXX=clang++ PLAYER_SMOKE_FLAGS='-fsanitize=address,undefined' ...
No Switch, credentials, or real audio/network service is used.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

STUBS = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t;
using s16=int16_t; using s64=int64_t; using Result=u32;
using LockableMutex=std::mutex;
constexpr size_t FS_MAX_PATH=768;
#define R_FAILED(rc) ((rc)!=0)
#define R_SUCCEEDED(rc) ((rc)==0)
#define R_UNLESS(cond,err) do { if (!(cond)) return (err); } while (0)
#define R_TRY(expr) do { const Result r_=(expr); if (r_) return r_; } while (0)
namespace qqmusic {
constexpr Result AudioUnavailable=1, FileOpenFailure=2, VoiceInitFailure=3,
    OutOfRange=4, InvalidArgument=5, NotPlaying=6, InvalidPath=7,
    OutOfMemory=8, QueueEmpty=9;
}
struct FsFile {};
constexpr int FsOpenMode_Read=1;
Result fsFileGetSize(FsFile*,s64 *size) { *size=0; return 0; }
Result fsFileRead(FsFile*,s64,void*,u64,int,u64 *size) { *size=0; return 0; }
void fsFileClose(FsFile*) {}
u64 randomGet64() { return 1; }
void svcSleepThread(u64 n) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<u64>(n,1000000)));
}
namespace sdmc {
Result OpenFile(FsFile*,const char*,int=0) { return 1; }
void CreateFolder(const char*) {}
void WriteFile(const char*,const void*,size_t) {}
void DeleteFile(const char*) {}
bool FileExists(const char*) { return true; }
}
namespace config {
float get_volume() { return 1; }
int get_repeat() { return 2; }
bool get_shuffle() { return false; }
float get_default_title_volume() { return 1; }
u32 get_queue_position() { return 0; }
bool has_title_enabled(u64) { return false; }
bool get_title_enabled(u64) { return true; }
bool get_title_enabled_default() { return true; }
bool has_title_volume(u64) { return false; }
float get_title_volume(u64) { return 1; }
void set_queue_position(u32) {}
void set_volume(float) {}
void set_repeat(int) {}
void set_shuffle(bool) {}
void set_default_title_volume(float) {}
void set_title_enabled(u64,bool) {}
void set_title_enabled_default(bool) {}
}
namespace pm { bool GetActiveApp(u64*,u64*) { return false; } }
void audWrapperSetProcessMasterVolume(u64,int,float) {}
struct GpioPadSession {};
enum GpioValue { GpioValue_Low, GpioValue_High };
Result gpioPadGetValue(GpioPadSession*,GpioValue*) { return 1; }
struct TrackMeta { char title[128]{},artist[128]{},album[128]{}; };
void MetaInit() {}
Result GetTrackMeta(const char*,TrackMeta*) { return 1; }
void SetTrackMetaCache(const char*,const TrackMeta&) {}
struct QqMusicOnlineSong { char songmid[64]{},media_mid[64]{},album_mid[64]{},title[128]{},artist[128]{},album[128]{}; };
namespace qqmusic::songcache { void Init() {} void SetQueueSize(u32) {} }
namespace qqmusic::api {
bool GetGuessRecommend(u32,u32,std::vector<QqMusicOnlineSong>&) { return false; }
}
struct AudioOutBuffer { void *buffer{}; size_t buffer_size{},data_size{}; };
enum AudioOutState { AudioOutState_Stopped, AudioOutState_Started };
namespace fake {
using namespace std::chrono_literals;
class Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool armed=false, entered=false, released=false;
public:
    void Arm() { std::lock_guard lk(mutex); armed=true; entered=false; released=false; }
    void Wait() {
        std::unique_lock lk(mutex);
        if (!armed) return;
        armed=false; entered=true; cv.notify_all();
        assert(cv.wait_for(lk,5s,[&] { return released; }));
    }
    void Entered() {
        std::unique_lock lk(mutex);
        assert(cv.wait_for(lk,5s,[&] { return entered; }));
    }
    void Release() { std::lock_guard lk(mutex); released=true; cv.notify_all(); }
};
struct Plan {
    Gate open, read, seek;
    std::atomic<int> opens{0},reads{0},seeks{0},destroyed{0};
    std::atomic<bool> fail_open{false},fail_read{false};
    std::atomic<u32> last_seek{0};
    int marker;
    explicit Plan(int marker):marker(marker) {}
};
std::map<std::string,std::shared_ptr<Plan>> plans;
std::mutex audio_mutex;
std::set<AudioOutBuffer*> queued;
struct Submission { s16 marker; bool started; };
std::vector<Submission> submissions;
AudioOutState audio_state=AudioOutState_Stopped;
void ClearSubmissions() { std::lock_guard lk(audio_mutex); submissions.clear(); }
std::vector<Submission> Submissions() { std::lock_guard lk(audio_mutex); return submissions; }
void CheckWritable(void *ptr) {
    std::lock_guard lk(audio_mutex);
    for (auto *buf:queued) assert(buf->buffer!=ptr);
}
template<class F> auto Responsive(F fn) {
    auto future=std::async(std::launch::async,fn);
    assert(future.wait_for(500ms)==std::future_status::ready);
    return future.get();
}
template<class F> void Until(F fn) {
    auto deadline=std::chrono::steady_clock::now()+5s;
    while (!fn()) {
        assert(std::chrono::steady_clock::now()<deadline);
        std::this_thread::sleep_for(1ms);
    }
}
}
Result audoutInitialize() { return 0; }
void audoutExit() {}
Result audoutSetAudioOutVolume(float) { return 0; }
int audoutGetChannelCount() { return 2; }
int audoutGetSampleRate() { return 48000; }
Result audoutGetAudioOutState(AudioOutState *s) { std::lock_guard lk(fake::audio_mutex); *s=fake::audio_state; return 0; }
Result audoutStartAudioOut() { std::lock_guard lk(fake::audio_mutex); fake::audio_state=AudioOutState_Started; return 0; }
Result audoutStopAudioOut() { std::lock_guard lk(fake::audio_mutex); fake::audio_state=AudioOutState_Stopped; return 0; }
Result audoutFlushAudioOutBuffers(bool *flushed) { std::lock_guard lk(fake::audio_mutex); fake::queued.clear(); *flushed=true; return 0; }
Result audoutGetReleasedAudioOutBuffer(AudioOutBuffer **buffer,u32 *count) {
    std::lock_guard lk(fake::audio_mutex);
    *buffer=nullptr; *count=0;
    if (fake::audio_state==AudioOutState_Started && !fake::queued.empty()) {
        *buffer=*fake::queued.begin(); fake::queued.erase(fake::queued.begin()); *count=1;
    }
    return 0;
}
Result audoutContainsAudioOutBuffer(AudioOutBuffer *buffer,bool *contains) { std::lock_guard lk(fake::audio_mutex); *contains=fake::queued.contains(buffer); return 0; }
Result audoutAppendAudioOutBuffer(AudioOutBuffer *buffer) {
    std::lock_guard lk(fake::audio_mutex);
    assert(!fake::queued.contains(buffer));
    fake::queued.insert(buffer);
    fake::submissions.push_back({static_cast<s16*>(buffer->buffer)[0],fake::audio_state==AudioOutState_Started});
    return 0;
}
enum class SourceType { NONE, MP3 };
SourceType GetSourceType(const char*) { return SourceType::MP3; }
class Source {
    std::shared_ptr<fake::Plan> plan;
    const std::thread::id owner=std::this_thread::get_id();
    u32 frame=0;
    void CheckOwner() const { assert(owner==std::this_thread::get_id()); }
public:
    explicit Source(std::shared_ptr<fake::Plan> p):plan(std::move(p)) {}
    ~Source() { CheckOwner(); ++plan->destroyed; }
    bool IsOpen() { CheckOwner(); return true; }
    bool SetupResampler(int,int) { CheckOwner(); return true; }
    std::pair<u32,u32> Tell() { CheckOwner(); return {frame,1000000000}; }
    int GetSampleRate() { CheckOwner(); return 48000; }
    bool Seek(u64 pos) {
        CheckOwner(); ++plan->seeks; plan->seek.Wait();
        frame=pos; plan->last_seek=pos; return true;
    }
    s64 Resample(u8 *buffer,size_t size) {
        CheckOwner(); fake::CheckWritable(buffer); ++plan->reads;
        const auto marker=plan->marker+frame/100000;
        plan->read.Wait();
        fake::CheckWritable(buffer);
        std::fill_n(reinterpret_cast<s16*>(buffer),size/sizeof(s16),static_cast<s16>(marker));
        frame+=size/4;
        return plan->fail_read ? -1 : static_cast<s64>(size);
    }
};
std::unique_ptr<Source> OpenFile(const char *path,const SourceIoRequest&) {
    auto plan=fake::plans.at(path); ++plan->opens; plan->open.Wait();
    if (plan->fail_open) return nullptr;
    return std::make_unique<Source>(plan);
}
'''

SCENARIOS = r'''
using namespace qqmusic::impl;
using namespace std::chrono_literals;
struct Session {
    std::shared_ptr<fake::Plan> a=std::make_shared<fake::Plan>(1000);
    std::shared_ptr<fake::Plan> b=std::make_shared<fake::Plan>(2000);
    std::thread worker;
    Session() {
        fake::plans={{"a.mp3",a},{"b.mp3",b}};
        fake::ClearSubmissions();
        g_should_run=true;
        g_audio_unavailable=false;
        g_last_play_failed=false;
        g_pause_reasons=static_cast<u32>(PauseReason::Manual);
        assert(Initialize()==0);
        assert(Enqueue("a.mp3",5,qqmusic::EnqueueType::Back,false)==0);
        assert(Enqueue("b.mp3",5,qqmusic::EnqueueType::Back,false)==0);
    }
    void Start() { Select(0); worker=std::thread([] { TuneThreadFunc(nullptr); }); }
    void CheckCurrent(u32 expected) {
        fake::Responsive([=] {
            qqmusic::CurrentStats stats{}; char path[FS_MAX_PATH]{};
            assert(GetCurrentQueueItem(&stats,path,sizeof(path))==0);
            assert(stats.index==expected);
            assert(std::string(path)==(expected==0?"a.mp3":"b.mp3"));
            assert(GetPlaylistSize()==2);
            (void)GetStatus();
        });
    }
    ~Session() {
        Exit(); a->open.Release(); a->read.Release(); a->seek.Release();
        b->open.Release(); b->read.Release(); b->seek.Release();
        if (worker.joinable()) worker.join();
        ClearQueue();
    }
};
void StaleOpenFailure() {
    Session s; s.a->open.Arm(); s.a->fail_open=true; s.b->read.Arm(); s.Start();
    s.a->open.Entered();
    fake::Responsive([] { Select(1); }); s.CheckCurrent(1);
    s.a->open.Release(); s.b->read.Entered();
    assert(GetStatus()); assert(!GetLastPlayError()); assert(GetPlaylistSize()==2);
    std::puts("PASS: blocked open permits select/status; stale open failure preserves selection");
}
void StaleReadAfterSelect(bool fail) {
    Session s; s.a->read.Arm(); s.a->fail_read=fail; s.b->read.Arm(); s.Start();
    s.a->read.Entered(); s.CheckCurrent(0);
    for (int i=0;i<20;++i) fake::Responsive([i] { Select(i%2); });
    s.CheckCurrent(1); fake::ClearSubmissions(); s.a->read.Release();
    s.b->read.Entered(); assert(fake::Submissions().empty());
    assert(GetStatus()); assert(!GetLastPlayError()); assert(GetPlaylistSize()==2);
    assert(s.a->opens==1); assert(s.b->opens==1); assert(s.a->destroyed==1);
    std::puts(fail ? "PASS: stale read failure cannot pause/remove the new track" :
        "PASS: repeated selection coalesces; old read cannot submit PCM after skip");
}
void CoalescedSeekWhilePaused() {
    Session s; s.a->read.Arm(); s.a->seek.Arm(); s.Start(); s.a->read.Entered();
    fake::Responsive([] { Pause(); Seek(100000); Seek(200000); }); s.CheckCurrent(0);
    fake::ClearSubmissions(); s.a->read.Release(); s.a->seek.Entered();
    assert(fake::Submissions().empty()); assert(s.a->seeks==1);
    fake::Responsive([] { Seek(300000); }); s.CheckCurrent(0);
    s.a->seek.Release(); fake::Until([&] { return s.a->seeks==2 && s.a->last_seek==300000; });
    fake::Until([] { qqmusic::CurrentStats stats{}; char path[FS_MAX_PATH]{};
        return GetCurrentQueueItem(&stats,path,sizeof(path))==0 && stats.current_frame==300000; });
    assert(fake::Submissions().empty()); assert(!GetStatus());
    Play(); fake::Until([] { return !fake::Submissions().empty(); });
    assert(fake::Submissions().front().marker==1003);
    std::puts("PASS: blocked seek permits status/new seek; latest seek wins while paused; pre-seek PCM discarded");
}
void SkipDuringSeek() {
    Session s; s.a->read.Arm(); s.a->seek.Arm(); s.b->read.Arm(); s.Start();
    s.a->read.Entered(); Seek(100000); s.a->read.Release(); s.a->seek.Entered();
    fake::Responsive([] { Next(); }); s.CheckCurrent(1);
    fake::ClearSubmissions(); s.a->seek.Release(); s.b->read.Entered();
    assert(fake::Submissions().empty()); assert(GetStatus()); assert(!GetLastPlayError());
    std::puts("PASS: skip during blocked seek cannot submit stale PCM or change new status");
}
void PauseDuringRead() {
    Session s; s.a->read.Arm(); s.Start(); s.a->read.Entered();
    fake::Responsive([] { Pause(); }); s.CheckCurrent(0); s.a->read.Release();
    fake::Until([] { return !fake::Submissions().empty(); });
    const auto paused=fake::Submissions();
    assert(paused.size()==1); assert(!paused.front().started); assert(paused.front().marker==1000);
    assert(!GetStatus()); Play(); fake::Until([] { return fake::Submissions().size()>1; });
    assert(GetStatus());
    std::puts("PASS: pause retains in-flight PCM without restarting output; play resumes");
}
void ReusedQueueSlot() {
    Session s; s.a->read.Arm(); s.b->read.Arm(); s.Start(); s.a->read.Entered();
    fake::Responsive([] {
        ClearQueue(); assert(Enqueue("b.mp3",5,qqmusic::EnqueueType::Back,false)==0); Select(0);
    });
    fake::ClearSubmissions(); s.a->read.Release(); s.b->read.Entered();
    assert(fake::Submissions().empty()); assert(GetStatus()); assert(!GetLastPlayError());
    qqmusic::CurrentStats stats{}; char path[FS_MAX_PATH]{};
    assert(GetCurrentQueueItem(&stats,path,sizeof(path))==0 && std::string(path)=="b.mp3");
    std::puts("PASS: clear/re-enqueue reuse cannot revive old playlist ID");
}
void QueueRemoval() {
    Session s;   // queue = [a.mp3, b.mp3]; no worker thread needed for pure queue edits
    char path[FS_MAX_PATH]{};
    assert(Enqueue("c.mp3",5,qqmusic::EnqueueType::Front,false)==0);          // [c,a,b]
    assert(GetPlaylistSize()==3);
    assert(GetPlaylistItem(0,path,sizeof(path))==0 && std::string(path)=="c.mp3");
    assert(Remove(1)==0);                                                    // drop a.mp3 -> [c,b]
    assert(GetPlaylistSize()==2);
    assert(GetPlaylistItem(0,path,sizeof(path))==0 && std::string(path)=="c.mp3");
    assert(GetPlaylistItem(1,path,sizeof(path))==0 && std::string(path)=="b.mp3");
    assert(Remove(1)==0 && GetPlaylistSize()==1);
    assert(GetPlaylistItem(0,path,sizeof(path))==0 && std::string(path)=="c.mp3");
    assert(Remove(0)==0 && GetPlaylistSize()==0);
    assert(Remove(0)!=0);                                                    // empty queue is an error

    // Under shuffle the index is a position in the shuffled order: removing it
    // must drop exactly that entry (the delete path no longer assumes index 0).
    assert(Enqueue("a.mp3",5,qqmusic::EnqueueType::Back,false)==0);
    assert(Enqueue("b.mp3",5,qqmusic::EnqueueType::Back,false)==0);
    assert(Enqueue("c.mp3",5,qqmusic::EnqueueType::Front,false)==0);
    SetShuffleMode(qqmusic::ShuffleMode::On);
    assert(GetPlaylistItem(0,path,sizeof(path))==0);
    const std::string victim(path);
    assert(Remove(0)==0);
    assert(GetPlaylistSize()==2);
    for (u32 i=0;i<GetPlaylistSize();++i) {
        assert(GetPlaylistItem(i,path,sizeof(path))==0);
        assert(std::string(path)!=victim);
    }
    SetShuffleMode(qqmusic::ShuffleMode::Off);
    std::puts("PASS: queue removal keeps order and drops exactly the selected entry");
}
int main() {
    StaleOpenFailure(); StaleReadAfterSelect(false); StaleReadAfterSelect(true);
    CoalescedSeekWhilePaused(); SkipDuringSeek(); PauseDuringRead(); ReusedQueueSlot();
    QueueRemoval();
    std::puts("PASS: production player concurrency smoke (decoder/audio fakes; not Switch runtime)");
}
'''


def without_includes(path):
    return "\n".join(line for line in path.read_text(encoding="utf-8").splitlines()
                     if not line.lstrip().startswith(("#include", "#pragma once")))


def main():
    # Compile real production implementation, not a second state-machine copy.
    source_header = (ROOT / "sysmodule/source/impl/source.hpp").read_text(encoding="utf-8")
    start = source_header.index("struct SourceIoRequest {")
    token = source_header[start:source_header.index("\n};", start) + 3]
    stubs = STUBS.replace("enum class SourceType", token + "\nenum class SourceType", 1)
    cpp = stubs + without_includes(ROOT / "sysmodule/source/types.hpp")
    cpp += without_includes(ROOT / "sysmodule/source/impl/player.hpp")
    cpp += without_includes(ROOT / "sysmodule/source/impl/player.cpp") + SCENARIOS
    with tempfile.TemporaryDirectory(prefix="qqmusic-player-smoke-") as temp:
        root = Path(temp)
        (root / "player-smoke.cpp").write_text(cpp, encoding="utf-8")
        command = shlex.split(os.environ.get("CXX", "g++")) + ["-std=c++20", "-pthread", "-g", "-O1"]
        command += shlex.split(os.environ.get("PLAYER_SMOKE_FLAGS", ""))
        command += [str(root / "player-smoke.cpp"), "-o", str(root / "player-smoke")]
        subprocess.run(command, check=True)
        subprocess.run([str(root / "player-smoke")], check=True, timeout=60)


if __name__ == "__main__":
    main()

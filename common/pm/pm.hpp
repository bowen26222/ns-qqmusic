#pragma once

#include <switch.h>

namespace pm {

auto Initialize() -> Result;
void Exit();
void getCurrentPidTid(u64* pid_out, u64* tid_out);
auto PollCurrentPidTid(u64* pid_out, u64* tid_out) -> bool;

// 查询当前活动的应用（游戏）。
// 返回 true：有真实应用程序在前台（pid_out/tid_out 有效）。
// 返回 false：无应用（桌面 / 启动器 applet 等），输出置 0。
auto GetActiveApp(u64* pid_out, u64* tid_out) -> bool;

}
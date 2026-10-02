#ifndef LILITH_AUTORUN_H
#define LILITH_AUTORUN_H

#include "lilith_timer.h"

/*
 * 开机自启 / 定时启动 管理模块
 *
 * 设计目标：稳定。三条自启通道互相独立、可共存，打开总开关时全部写入，
 * 启动/保存设置时逐个校验、缺失的自动补写：
 *   1. 注册表 HKCU\...\Run                    —— 每次开机（登录）启动
 *   2. 启动文件夹快捷方式 Startup\LilithTimer.lnk —— 同上，双保险
 *   3. 计划任务 LilithTimer_AutoStart          —— 登录触发 或 每日定时触发
 *
 * AutoStartMode=1（每次开机）：三条通道全部指向「登录即启动」。
 * AutoStartMode=2（定时启动）：登录通道保留（兜底），计划任务改为
 *   每日 AutoStartTime 触发；如果用户开机晚于该时刻，登录通道兜底拉起。
 */

/* 计划任务名称（注册表键名 / 快捷方式文件名见 autorun.c 内部常量） */
#define AUTORUN_TASK_NAME  L"LilithTimer_AutoStart"

/*
 * 同步自启状态：cfg 为期望状态（Enabled/Mode/StartMinutes），
 * 按需创建 / 更新 / 删除三条通道，并对现存项做校验修复。
 * 全程不打断用户：任何子步骤失败仅写诊断日志。
 */
void AutoRunSync(BOOL enabled, int mode, int startMinutes);

/*
 * 校验现有自启项是否仍然指向当前 exe（exe 被移动后注册表会指向旧路径），
 * 发现失效项就地修复。程序启动时调用一次即可（cfg 已加载）。
 */
void AutoRunVerify(void);

#endif

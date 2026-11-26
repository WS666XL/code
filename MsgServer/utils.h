#pragma once
#include <string>
// 时间工具：返回 "YYYY-MM-DD HH:MM:SS" 格式的本地时间字符串，
// 用于消息落库的 created_at/updated_at 字段
std::string getCurrentTimestamp();

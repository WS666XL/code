#pragma once
#include <hiredis.h>
#include <string>
/******************************************************************************
 *  文件：DistLock.h
 *  作用：基于 Redis 的分布式锁（单例）。
 *  原理：加锁用 SET key uuid NX EX timeout（不存在才能写入，天然互斥）；
 *        解锁用 Lua 脚本"先比对锁的值再删除"，保证只有持锁人能解锁，
 *        避免 A 的锁超时释放后误删了 B 刚抢到的锁。
 *  场景：同一账号在两台 ChatServer 同时登录时，用锁保证踢人流程串行执行。
 *****************************************************************************/
class DistLock
{
public:
	static DistLock& Inst();
	~DistLock() = default;
	// 尝试抢锁，成功返回本次锁的 uuid 标识，失败（超时）返回空串
	std::string acquireLock(redisContext* context, const std::string& lockName,
		int lockTimeout, int acquireTimeout);

	// 释放锁，只有 identifier 与持锁标识一致时才会真正删除
	bool releaseLock(redisContext* context, const std::string& lockName,
		const std::string& identifier);
private:
	DistLock() = default;
};

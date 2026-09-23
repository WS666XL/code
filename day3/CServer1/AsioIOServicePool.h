#pragma once
#include <vector>
#include <boost/asio.hpp>
#include "Singleton.h"
#include"const.h"
// Asio I/O 线程池：内部持有若干个 io_context，每个跑在自己的线程上，新连接轮询分发
class AsioIOServicePool : public Singleton<AsioIOServicePool>
{
    friend Singleton<AsioIOServicePool>;

public:
    using IOService = boost::asio::io_context;
    using Work = boost::asio::executor_work_guard<IOService::executor_type>;
    using WorkPtr = std::unique_ptr<Work>;

    ~AsioIOServicePool();

    AsioIOServicePool(const AsioIOServicePool&) = delete;
    AsioIOServicePool& operator=(const AsioIOServicePool&) = delete;

    // 使用 round-robin 的方式返回一个 io_context
    IOService& GetIOService();      // 轮询取一个 io_context，把新连接分散到不同线程，避免单线程排队

    void Stop();                    // 停止线程池：释放 work_guard 并 join 所有 io 线程

private:
    AsioIOServicePool(std::size_t size = 2);

    std::vector<IOService> _ioServices;      // 所有 io_context，下标即线程编号
    std::vector<WorkPtr> _works;             // 每个 io_context 的 work_guard，防止 run() 没任务时提前返回
    std::vector<std::thread> _threads;       // 每个 io_context 对应的执行线程
    std::size_t _nextIOService;              // 轮询游标：记录下一个该派发的 io_context 下标
};

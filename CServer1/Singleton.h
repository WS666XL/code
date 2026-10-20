// Singleton.h —— 单例模板类（CRTP：class X : public Singleton<X>）
#pragma once
#include <memory>
#include <mutex>
#include <iostream>

template <typename T>
class Singleton {
protected:
    Singleton() = default;                            // protected：外部不能直接构造

    Singleton(const Singleton<T>&) = delete;          // 禁用拷贝与赋值，保证唯一性
    Singleton& operator=(const Singleton<T>& st) = delete;

    static std::shared_ptr<T> _instance;              // 唯一实例

public:
    static std::shared_ptr<T> GetInstance() {         // 获取全局唯一实例
        static std::once_flag s_flag;                 // call_once：new T 只执行一次，线程安全
        std::call_once(s_flag, [&]() {
            _instance = std::shared_ptr<T>(new T);
            });

        return _instance;                             // 之后每次都返回同一个指针
    }

    // 调试用：打印实例地址
    void PrintAddress() {
        std::cout << _instance.get() << std::endl;
    }

    // 析构：打印一句，便于观察销毁时机
    ~Singleton() {
        std::cout << "this is singleton destruct" << std::endl;
    }
};

// 类外初始化静态成员（模板静态成员的定义必须放在头文件里）
template <typename T>
std::shared_ptr<T> Singleton<T>::_instance = nullptr;

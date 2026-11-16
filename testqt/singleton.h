#ifndef SINGLETON_H
#define SINGLETON_H
#include <memory>
#include <mutex>
#include <iostream>
using namespace std;

// ===== 单例模板 =====
// 任何类只要写上 class Foo : public Singleton<Foo>，它就变成"全局唯一实例"。
// 使用方式：HttpMgr::GetInstance() 拿到的永远是同一个对象，不需要自己 new。
template <typename T>
class Singleton {
protected:
    // 构造/拷贝/赋值都是 protected 或 deleted —— 外部无法自己创建，只能走 GetInstance()
    Singleton() = default;
    Singleton(const Singleton<T>&) = delete;
    Singleton& operator=(const Singleton<T>& st) = delete;

    static std::shared_ptr<T> _instance;   // 用 shared_ptr 持有唯一实例，引用计数为 0 时自动析构
public:
    static std::shared_ptr<T> GetInstance() {
        static std::once_flag s_flag;      // std::call_once 保证多线程下也只 new 一次
        std::call_once(s_flag, [&]() {
            _instance = shared_ptr<T>(new T);
        });

        return _instance;
    }
    void PrintAddress() {                  // 调试用：打印实例地址，可验证两次调用拿到的是同一个对象
        std::cout << _instance.get() << endl;
    }
    ~Singleton() {
        std::cout << "this is singleton destruct" << std::endl;
    }
};

// 类模板的静态成员变量必须在类外再定义一次
template <typename T>
std::shared_ptr<T> Singleton<T>::_instance = nullptr;
#endif // SINGLETON_H

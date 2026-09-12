// ============================================================================
// Singleton.h —— 单例「模板类」
//
// 什么是单例？
//   保证某个类在整个程序运行期间「只有一个对象」，并且提供一个全局访问点。
//   本项目里 LogicSystem（逻辑处理中心）就只需要一个，所以用单例。
//
// 为什么要写成模板 template <typename T>？
//   这样任何类想变成单例，只要写 class X : public Singleton<X> 就行，不用重复写一遍代码。
//   这种手法叫 CRTP（把子类自己的类型当作模板参数传给父类）。
//
// 怎么用？
//   class LogicSystem : public Singleton<LogicSystem> { ... }
//   LogicSystem::GetInstance()->某成员函数();
// ============================================================================

#pragma once
#include <memory>
#include <mutex>
#include <iostream>

template <typename T>
class Singleton {
protected:
    // 构造函数设为 protected：外面不能写 Singleton<T> s; 也不能 new，
    // 但子类可以继承（子类构造时会调用它）。这样就把「创建对象」的权力收归 GetInstance 了。
    Singleton() = default;

    // =delete 表示「禁用」：删掉拷贝构造函数和赋值运算符，
    // 防止有人写 Singleton<T> b = a; 复制出第二个实例，破坏唯一性。
    Singleton(const Singleton<T>&) = delete;
    Singleton& operator=(const Singleton<T>& st) = delete;

    // 静态成员变量：整个类共享一份，用来保存那唯一的实例（下面在类外初始化）
    static std::shared_ptr<T> _instance;

public:
    // 【核心函数】获取全局唯一实例
    static std::shared_ptr<T> GetInstance() {
        // once_flag + call_once 是 C++11 提供的「只执行一次」机制，
        // 即使多个线程同时调用 GetInstance，new T 也只会发生一次（线程安全）。
        static std::once_flag s_flag;
        std::call_once(s_flag, [&]() {
            // 第一次调用时才真正创建对象；注意这里 new 的是模板参数 T（也就是子类类型）
            _instance = std::shared_ptr<T>(new T);
            });

        // 之后每次调用都直接返回同一个指针
        return _instance;
    }

    // 调试用：打印这个唯一实例的内存地址。
    // 在两处代码里各打印一次，如果地址相同，就证明拿到的是同一个对象。
    // （小提示：这里的 endl 没写 std::，严格来说应写成 std::endl；
    //  因为模板成员函数只有在被调用时才会实例化，这个函数一直没被调用，所以目前编译没报错）
    void PrintAddress() {
        std::cout << _instance.get() << std::endl;
    }

    // 析构函数：程序结束时打印一句话，方便观察单例什么时候被销毁
    ~Singleton() {
        std::cout << "this is singleton destruct" << std::endl;
    }
};

// 类外初始化静态成员。
// 模板类的静态成员定义必须写在头文件里（编译器要在每个用到它的地方看到定义），
// 初始化为 nullptr，表示「一开始还没有实例，等第一次 GetInstance 时再创建」。
template <typename T>
std::shared_ptr<T> Singleton<T>::_instance = nullptr;
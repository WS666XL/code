// ConfigMgr.h —— 配置管理器：把 config.ini 一次性读进内存，之后全程序按 [section][key] 取值
// 用法：gCfgMgr["GateServer"]["Port"]
#pragma once
#include"const.h"

// 一个 ini 段落（形如 [GateServer]）对应的 key-value 集合
struct SectionInfo {
    SectionInfo() {}
    ~SectionInfo() {
        _section_datas.clear();
    }

    // 拷贝构造：map 的赋值本身是深拷贝，直接搬即可
    SectionInfo(const SectionInfo& src) {
        _section_datas = src._section_datas;
    }

    // 拷贝赋值
    SectionInfo& operator = (const SectionInfo& src) {
        if (&src == this) {
            return *this;              // 自赋值直接返回，省掉一次无谓的拷贝
        }

        this->_section_datas = src._section_datas;
        // 【注意】原代码到此结束，缺少 return *this;（函数声明了返回引用），属未定义行为，
        // 只是当前没有任何地方用到 operator= 的返回值才没暴露。此处仅记录，未改动代码。
    }

    std::map<std::string, std::string> _section_datas;   // key → value

    // 按 key 取值；key 不存在时返回空字符串，不抛异常、也不会往表里插新元素
    std::string  operator[](const std::string& key) {
        if (_section_datas.find(key) == _section_datas.end()) {
            return "";
        }
        // 这里可以添加一些边界检查  
        return _section_datas[key];
    }
};

// 整个 ini 文件：section 名 → 该 section 的 key-value 集合
class ConfigMgr
{
public:
    ~ConfigMgr() {
        _config_map.clear();
    }

    // 按 section 名取段落，取到之后再接一个 [key]：gCfgMgr["GateServer"]["Port"]
    SectionInfo operator[](const std::string& section) {
        if (_config_map.find(section) == _config_map.end()) {
            return SectionInfo();      // 段落不存在 → 返回空对象，后续 [key] 自然得到 ""
        }
        return _config_map[section];
    }


    ConfigMgr& operator=(const ConfigMgr& src) = delete;
        /*{         // 拷贝赋值
        if (&src == this) {
            return *this;
        }

        this->_config_map = src._config_map;
        return *this;
    };*/

	ConfigMgr(const ConfigMgr& src) = delete;   
        /*{                    // 拷贝构造
        this->_config_map = src._config_map;

    }*/
    static ConfigMgr& Inst() {
        static ConfigMgr cfg_mgr;
        return cfg_mgr;
    }
                                       
private:
    ConfigMgr();
    // 存储section和key-value对的map  
    std::map<std::string, SectionInfo> _config_map;
};

// ConfigMgr.cpp —— 构造时把 config.ini 读进 _config_map，之后全程查内存，不再读盘
#include "ConfigMgr.h"

ConfigMgr::ConfigMgr() {
    // 获取当前工作目录  
    // 注意这里取的是「进程的当前工作目录」，不是 exe 所在目录：
    // VS 里 F5 调试默认是工程目录，双击 exe 启动则是 exe 所在目录 —— config.ini 放错地方就读不到
    boost::filesystem::path current_path = boost::filesystem::current_path();
    // 构建config.ini文件的完整路径  
    boost::filesystem::path config_path = current_path / "config.ini";
    std::cout << "Config path: " << config_path << std::endl;

    // 使用Boost.PropertyTree来读取INI文件  
    boost::property_tree::ptree pt;                             // ptree 是通用树，读 ini 后层级为 section → key → value
    boost::property_tree::read_ini(config_path.string(), pt);   // 文件不存在会直接抛异常


    // 遍历INI文件中的所有section  
    for (const auto& section_pair : pt) {
        const std::string& section_name = section_pair.first;                     // 段落名，如 "GateServer"
        const boost::property_tree::ptree& section_tree = section_pair.second;    // 该段落对应的子树

        // 对于每个section，遍历其所有的key-value对  
        std::map<std::string, std::string> section_config;                        // 先把本段落攒成一个 map
        for (const auto& key_value_pair : section_tree) {
            const std::string& key = key_value_pair.first;                        // 键，如 "Port"
            const std::string& value = key_value_pair.second.get_value<std::string>();   // 值，统一按字符串取
            section_config[key] = value;
        }
        SectionInfo sectionInfo;
        sectionInfo._section_datas = section_config;
        // 将section的key-value对保存到config_map中  
        _config_map[section_name] = sectionInfo;      // 至此完成 "section → (key → value)" 的两层映射
    }

    // 输出所有的section和key-value对  
    for (const auto& section_entry : _config_map) {
        const std::string& section_name = section_entry.first;
        SectionInfo section_config = section_entry.second;
        std::cout << "[" << section_name << "]" << std::endl;
        for (const auto& key_value_pair : section_config._section_datas) {
            std::cout << key_value_pair.first << "=" << key_value_pair.second << std::endl;
        }
    }

}

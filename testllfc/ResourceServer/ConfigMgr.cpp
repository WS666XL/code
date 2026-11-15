#include "ConfigMgr.h"
ConfigMgr::ConfigMgr(){
	// 读取当前工作目录
	boost::filesystem::path current_path = boost::filesystem::current_path();
	// 拼接config.ini文件相对路径
	boost::filesystem::path config_path = current_path / "config.ini";
	std::cout << "Config path: " << config_path << std::endl;

	// 使用Boost.PropertyTree读取INI文件
	boost::property_tree::ptree pt;
	boost::property_tree::read_ini(config_path.string(), pt);

	// 遍历INI文件中的所有section
	for (const auto& section_pair : pt) {
		const std::string& section_name = section_pair.first;
		const boost::property_tree::ptree& section_tree = section_pair.second;

		// 遍历每个section下的所有key-value对
		std::map<std::string, std::string> section_config;
		for (const auto& key_value_pair : section_tree) {
			const std::string& key = key_value_pair.first;
			const std::string& value = key_value_pair.second.get_value<std::string>();
			section_config[key] = value;
		}
		SectionInfo sectionInfo;
		sectionInfo._section_datas = section_config;
		// 将section的key-value对保存到config_map中
		_config_map[section_name] = sectionInfo;
	}

	// 打印所有的section和key-value对（启动时自检配置是否读对）
	for (const auto& section_entry : _config_map) {
		const std::string& section_name = section_entry.first;
		SectionInfo section_config = section_entry.second;
		std::cout << "[" << section_name << "]" << std::endl;
		for (const auto& key_value_pair : section_config._section_datas) {
			std::cout << key_value_pair.first << "=" << key_value_pair.second << std::endl;
		}
	}

	// 初始化文件落盘目录
	InitPath();
}

std::string ConfigMgr::GetValue(const std::string& section, const std::string& key) {
	if (_config_map.find(section) == _config_map.end()) {
		return "";
	}

	return _config_map[section].GetValue(key);
}

// 文件输出目录 = 工作目录 / [Output] Path / [Static] Path
// 比如 work/output/static，上传的文件都写到这个目录下
boost::filesystem::path ConfigMgr::GetFileOutPath()
{
	return _static_path;
}

void ConfigMgr::InitPath()
{
	// 获取当前工作目录
	boost::filesystem::path current_path = boost::filesystem::current_path();
	std::string bindir = _config_map["Output"].GetValue("Path");
	std::string staticdir = _config_map["Static"].GetValue("Path");
	_static_path = current_path / bindir / staticdir;

	// 路径不存在则创建（多级目录一次性建好）
	if (!boost::filesystem::exists(_static_path)) {
		boost::filesystem::create_directories(_static_path);
	}
	std::cout << "FileOutPath is " << _static_path << std::endl;
}

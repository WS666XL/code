#pragma once
#include "const.h"
#include "Singleton.h"
#include <map>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

//一个配置段（ini 里的 [Section]），支持下标访问 key
class SectionInfo {
	friend class ConfigMgr;
public:
	SectionInfo() = default;
	~SectionInfo() {
		_section_datas.clear();
	}
	//C++11 以后 const string& 作为 map 下标会产生临时对象，统一改成传值
	SectionInfo(const SectionInfo& other) : _section_datas(other._section_datas) {}
	SectionInfo& operator=(const SectionInfo& other) {
		if (this == &other) {
			return *this;
		}
		this->_section_datas = other._section_datas;
		return *this;
	}

	std::string operator[](const std::string& key) {
		if (_section_datas.find(key) == _section_datas.end()) {
			return "";
		}
		return _section_datas[key];
	}

	std::string GetValue(const std::string& key) {
		if (_section_datas.find(key) == _section_datas.end()) {
			return "";
		}
		return _section_datas[key];
	}

	std::map<std::string, std::string> _section_datas;
};

//配置管理单例：读取工作目录下的 config.ini
class ConfigMgr : public Singleton<ConfigMgr>
{
	friend class Singleton<ConfigMgr>;
public:
	//兼容 ChatServer 的调用习惯：ConfigMgr::Inst()
	static ConfigMgr& Inst() {
		static ConfigMgr cfg;
		return cfg;
	}
	~ConfigMgr() {
		_config_map.clear();
	}
	SectionInfo operator[](const std::string& section_name) {
		if (_config_map.find(section_name) == _config_map.end()) {
			return SectionInfo();
		}
		return _config_map[section_name];
	}

	ConfigMgr(const ConfigMgr&) = delete;
	ConfigMgr& operator=(const ConfigMgr&) = delete;

	std::string GetValue(const std::string& section, const std::string& key);

	//文件输出目录（上传文件的落盘位置）
	boost::filesystem::path GetFileOutPath();
private:
	ConfigMgr();
	void InitPath();  //创建落盘目录（不存在时）
	std::map<std::string, SectionInfo> _config_map;
	boost::filesystem::path _static_path;  //最终落盘目录
};

// VarifyGrpcClient.cpp —— 本该放客户端实现，但本类所有函数都写在头文件里（内联定义），
// 所以这里只剩一句包含。作用是让 VS 把这份代码编进工程（.vcxproj 里登记的是 .cpp 而不是 .h）。
//
// 编译上完全没问题；以后若把 GetVarifyCode 的实现从头文件挪出来，就写在这个文件里。
#include "VarifyGrpcClient.h"
VerifyGrpcClient::VerifyGrpcClient() {
    auto& gCfgMgr = ConfigMgr::Inst();
    std::string host = gCfgMgr["VarifyServer"]["Host"];
    std::string port = gCfgMgr["VarifyServer"]["Port"];
    pool_.reset(new RPConPool(5, host, port));
}

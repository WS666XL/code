#include"global.h"

// 后端服务器地址前缀，先给个空串占位；
// 真正的值在程序启动时(main.cpp)从 config.ini 里读出来再赋给它
QString gate_url_prefix = "";

// repolish 的具体实现：先取消当前样式(polish)，再重新套用样式(unpolish)
// 合起来的效果 = 强制控件按最新属性重新匹配一次 QSS
std::function<void(QWidget*)>repolish=[](QWidget*w){

    w->style()->unpolish(w);
    w->style()->polish(w);
};

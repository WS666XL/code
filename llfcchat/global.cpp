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
std::function<QString(QString)> xorString = [](QString input){
    QString result = input; // 复制原始字符串，以便进行修改
    int length = input.length(); // 获取字符串的长度
    ushort xor_code = length % 255;
    for (int i = 0; i < length; ++i) {
        // 对每个字符进行异或操作
        // 注意：这里假设字符都是ASCII，因此直接转换为QChar
        result[i] = QChar(static_cast<ushort>(input[i].unicode() ^ xor_code));
    }
    return result;
};

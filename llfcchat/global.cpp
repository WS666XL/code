#include"global.h"
#include <QPixmapCache>

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

// 头像统一加载入口：原图只解码一次（存进 QPixmapCache），缩放结果按"路径@尺寸"缓存。
// 为什么要它：聊天列表、联系人列表、资料页、搜索弹窗会对同一张头像各自
// QPixmap(path) 全图解码 + SmoothTransformation 缩放，条目一多主线程就卡。
QPixmap LoadAvatarPixmap(const QString& path, const QSize& size)
{
    static QHash<QString, QPixmap> scaled_cache;   // 最终尺寸结果缓存
    const QString key = path + "@" + QString::number(size.width())
                              + "x" + QString::number(size.height());
    auto it = scaled_cache.find(key);
    if (it != scaled_cache.end()) {
        return it.value();
    }

    QPixmap pixmap;
    if (!QPixmapCache::find(path, &pixmap)) {      // 原图只解码一次
        pixmap = QPixmap(path);
        QPixmapCache::insert(path, pixmap);
    }

    QPixmap result;
    if (pixmap.isNull() || size.isEmpty()) {
        result = pixmap;
    } else {
        // KeepAspectRatioByExpanding：铺满头像框，宁可裁剪不留黑边
        result = pixmap.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    scaled_cache.insert(key, result);
    return result;
}

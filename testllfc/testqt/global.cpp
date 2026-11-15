#include"global.h"
#include <QPixmapCache>
#include <QLabel>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDebug>
#include "usermgr.h"
#include "filetcpmgr.h"

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

// ===== 头像下载回显（day39 后半段）=====
// 本地缺头像文件时 LoadAvatarPixmap 会在这里注册"待刷新的 label"并触发 1033 下载；
// 下载完成（FileTcpMgr 发 sig_avatar_downloaded）后遍历注册表重刷匹配的 label。
// label 存 QPointer 弱引用：列表条目销毁后指针自动置空，遍历时剔除，不会悬空访问
namespace {
struct PendingAvatarLabel {
    QPointer<QLabel> label;
    QString icon;
    QSize size;
    int owner_uid;
};
QList<PendingAvatarLabel>& PendingAvatarLabels()
{
    static QList<PendingAvatarLabel> list;
    return list;
}

// 确保下载完成信号只 connect 一次（静态初始化保证线程安全的懒加载语义）
void EnsureAvatarSignalConnected()
{
    static const bool connected = []() {
        QObject::connect(FileTcpMgr::GetInstance().get(), &FileTcpMgr::sig_avatar_downloaded,
            [](const QString& name) {
                auto& list = PendingAvatarLabels();
                for (auto it = list.begin(); it != list.end();) {
                    if (it->icon != name) {
                        ++it;
                        continue;
                    }
                    if (it->label) {
                        // 本地文件此时已就绪：allow_download=false 防止递归触发下载
                        it->label->setPixmap(
                            LoadAvatarPixmap(it->icon, it->size, it->owner_uid));
                        ++it;
                    } else {
                        // label 已随列表条目销毁：从注册表剔除
                        it = list.erase(it);
                    }
                }
            });
        return true;
    }();
    Q_UNUSED(connected);
}
} // namespace

// 头像统一加载入口：原图只解码一次（存进 QPixmapCache），缩放结果按
// "icon#所有者uid@尺寸"缓存。为什么要它：聊天列表、联系人列表、资料页、
// 搜索弹窗会对同一张头像各自 QPixmap(path) 全图解码 + SmoothTransformation 缩放，
// 条目一多主线程就卡。缓存 key 带上 owner_uid：不同用户可以同名 head.png，
// 不带的话 A 的头像下载完成后会污染 B 的缓存条目
QPixmap LoadAvatarPixmap(const QString& icon, const QSize& size, int owner_uid)
{
    // 不传 owner_uid（0）就当作"自己的头像"，用当前登录 uid
    if (owner_uid <= 0) {
        owner_uid = UserMgr::GetInstance()->GetUid();
    }

    static QHash<QString, QPixmap> scaled_cache;   // 最终尺寸结果缓存
    const QString key = icon + "#" + QString::number(owner_uid)
                              + "@" + QString::number(size.width())
                              + "x" + QString::number(size.height());
    auto it = scaled_cache.find(key);
    if (it != scaled_cache.end()) {
        return it.value();
    }

    // ---- 解析 icon 的三种形态到可加载的路径 ----
    QString load_path;
    bool is_res_path = false;   // true：Qt 资源路径（":/res/..."），不走文件系统
    static const QRegularExpression num_regex("^\\d+$");
    if (icon.startsWith(":/")) {
        // 形态 1：随包默认头像资源路径，直接用
        load_path = icon;
        is_res_path = true;
    } else if (num_regex.match(icon).hasMatch()) {
        // 形态 2：老数据默认头像编号（DB 里 icon="1"~"8"），转成资源路径
        int head_idx = icon.toInt();
        if (head_idx < 1 || head_idx > heads.size()) {
            head_idx = 1;   // 编号越界兜底到第一张
        }
        load_path = heads[static_cast<size_t>(head_idx - 1)];
        is_res_path = true;
    } else if (icon.contains('/')) {
        // 本地绝对/相对路径（调用点自己拼好的路径），直接尝试加载
        load_path = icon;
    } else {
        // 形态 3：服务器文件名（如 "head.png"）——
        // 统一拼到 AppData/<所有者uid>/avatars/ 下读本地缓存
        QString storage_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        load_path = storage_dir + "/" + QString::number(owner_uid)
                    + "/avatars/" + QFileInfo(icon).fileName();
    }

    QPixmap pixmap;
    if (is_res_path) {
        pixmap = QPixmap(load_path);          // 资源路径不走 QPixmapCache 的文件路径逻辑
        QPixmapCache::insert(icon, pixmap);
    } else if (!QPixmapCache::find(load_path, &pixmap)) {
        pixmap = QPixmap(load_path);
        QPixmapCache::insert(load_path, pixmap);
    }

    QPixmap result;
    if (!pixmap.isNull() && !size.isEmpty()) {
        // KeepAspectRatioByExpanding：铺满头像框，宁可裁剪不留黑边
        result = pixmap.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    } else {
        result = pixmap;
    }

    // 加载成功才写缓存；失败（本地还没有该头像文件）不缓存——
    // 否则下载完成后这个 key 依然命中失败结果，头像永远刷不出来
    if (!pixmap.isNull()) {
        scaled_cache.insert(key, result);
        return result;
    }

    // ---- 加载失败：默认头像兜底 + 异步下载回显 ----
    // 只有"服务器文件名"形态才值得下载（本地路径失败是文件真丢了，默认头像兜底即可）
    if (!is_res_path && !icon.contains('/') && !icon.isEmpty()) {
        EnsureAvatarSignalConnected();
        FileTcpMgr::GetInstance()->DownloadAvatar(icon, owner_uid);
    }
    // 先显示默认头像占位；下载完成后 sig_avatar_downloaded 会把注册的 label 重刷
    QPixmap fallback = QPixmap(":/res/head_1.jpg");
    if (!size.isEmpty()) {
        fallback = fallback.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    return fallback;
}

// 带 label 的重载：与无 label 版共用实现思路，差别只是失败下载时
// 把 label 注册进"待刷新表"，下载完成后自动把新头像 set 回去
QPixmap LoadAvatarPixmap(QLabel* label, const QString& icon, const QSize& size, int owner_uid)
{
    QPixmap result = LoadAvatarPixmap(icon, size, owner_uid);
    // 只有"服务器文件名"形态才注册待刷新（资源路径/纯数字/本地路径
    // 都能同步加载成功，不会触发下载，注册了也永远不会被刷新，只会堆积）。
    // 纯文件名的判断：不以 ":/" 开头、不含路径分隔符、不是纯数字编号
    static const QRegularExpression num_regex("^\\d+$");
    if (label && !icon.isEmpty() && !icon.startsWith(":/")
        && !icon.contains('/') && !num_regex.match(icon).hasMatch()) {
        auto& list = PendingAvatarLabels();
        bool exists = false;
        for (const auto& item : list) {
            if (item.label == label && item.icon == icon) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            if (owner_uid <= 0) {
                owner_uid = UserMgr::GetInstance()->GetUid();
            }
            list.append({label, icon, size, owner_uid});
        }
    }
    return result;
}

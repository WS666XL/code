#include"global.h"
#include <QPixmapCache>
#include <QLabel>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include "usermgr.h"
#include "filetcpmgr.h"
#include "tcpmgr.h"

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

// 前置声明：下载完成回调里要清缓存后再重载（定义在后文）
void ClearAvatarMemCache(int owner_uid);

// 确保下载完成信号只 connect 一次（静态初始化保证线程安全的懒加载语义）
void EnsureAvatarSignalConnected()
{
    static const bool connected = []() {
        QObject::connect(FileTcpMgr::GetInstance().get(), &FileTcpMgr::sig_file_downloaded,
            [](const QString& name) {
                auto& list = PendingAvatarLabels();
                for (auto it = list.begin(); it != list.end();) {
                    if (it->icon != name) {
                        ++it;
                        continue;
                    }
                    if (it->label) {
                        // 本地文件此时已就绪。必须先清一次两级缓存：
                        // 1057 到达时聊天页会立即重刷气泡，用"还没更新的旧文件"
                        // 又往缓存里填了一份旧像素，不清的话这里读到的还是旧图
                        ClearAvatarMemCache(it->owner_uid);
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

// 缩放结果缓存（原在 LoadAvatarPixmap 函数内，挪到此处是为了让
// InvalidateAvatarCache 也能清它——收到 1057 通知后按 owner_uid 剔除旧头像）
QHash<QString, QPixmap>& ScaledCache()
{
    static QHash<QString, QPixmap> cache;
    return cache;
}

// 判断 icon 是否为"服务器文件名"形态（形态 3）：这类 icon 才对应
// AppData/<uid>/avatars/ 下的本地缓存文件，需要清 QPixmapCache
bool IsServerFileNameIcon(const QString& icon)
{
    static const QRegularExpression num_regex("^\\d+$");
    return !icon.isEmpty() && !icon.startsWith(":/")
        && !icon.contains('/') && !num_regex.match(icon).hasMatch();
}

// 清理指定用户头像的两级内存缓存（scaled_cache 按 uid 剔除；
// QPixmapCache 按文件路径 remove——不清的话文件更新后旧图还在
// Qt 全局缓存里，重加载仍命中旧像素）。只清内存、不动本地文件。
// 注意：不区分 icon 形态，默认编号/资源路径的条目也要清——
// 首次换头像时本地 icon 还是 "1"，正是这些条目会挡住新图
void ClearAvatarMemCache(int owner_uid)
{
    QString uid_str = QString::number(owner_uid);
    QString avatar_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + "/" + uid_str + "/avatars";

    auto& cache = ScaledCache();
    for (auto it = cache.begin(); it != cache.end();) {
        // key 格式 "icon#uid@WxH"：icon 不含 '#'，按 '#' 和 '@' 拆出 uid
        const QString& key = it.key();
        int hash_pos = key.indexOf('#');
        int at_pos = key.lastIndexOf('@');
        if (hash_pos < 0 || at_pos < hash_pos) {
            ++it;
            continue;
        }
        if (key.mid(hash_pos + 1, at_pos - hash_pos - 1) != uid_str) {
            ++it;
            continue;
        }
        QString icon = key.left(hash_pos);
        if (IsServerFileNameIcon(icon)) {
            QPixmapCache::remove(avatar_dir + "/" + QFileInfo(icon).fileName());
        }
        it = cache.erase(it);
    }
}

// 收到 1057 头像变更通知后的本地处理（uid=换了头像的用户，icon=新文件名）：
// 1. 清两级内存缓存：scaled_cache 按 uid 剔除；QPixmapCache 按文件路径 remove
// 2. 把注册表里所有正在显示此人头像的 label 记录改成新文件名——
//    登录时本地 icon 可能还是默认编号（如 "1"）或资源路径，1057 之前
//    客户端根本不知道服务器上的新名字，不统一对齐，下载完也刷不上
// 3. 强制下载新文件：不能依赖"缓存里恰好有旧服务器文件名"来触发下载
//    （首次换头像时缓存键是 "1#uid@..."，扫不出可下载的名字）
//
// 为什么不再删除本地缓存文件：同一台机器上多开客户端时它们共享同一份
// AppData 缓存目录，A 端删除会把 B 端刚保存的本地头像文件一并删掉，
// 且两进程并发下载写同一个文件会互相踩踏（读到半截文件 → 加载失败 →
// 再次下载 → 无限循环）。改为只清内存缓存 + 后台下载：
// 下载期间界面保持旧图（内存 pixmap），新文件写好后自动切换
void InvalidateAvatarCache(int owner_uid, const QString& icon)
{
    // 自己刚上传完头像，本地与界面已是新图，跳过
    if (owner_uid <= 0 || owner_uid == UserMgr::GetInstance()->GetUid()) {
        return;
    }

    ClearAvatarMemCache(owner_uid);

    if (!icon.isEmpty()) {
        // 显示此人头像的 label 全部改成新文件名（下载完成后按它匹配刷新）
        for (auto& item : PendingAvatarLabels()) {
            if (item.owner_uid == owner_uid) {
                item.icon = icon;
            }
        }
        // 无论本地是否已有同名文件都重新拉一次：内容可能已变
        FileTcpMgr::GetInstance()->DownloadAvatar(icon, owner_uid);
    }
    qDebug() << "avatar cache invalidated for uid" << owner_uid
             << "new icon" << icon;
}

// 确保 1057 头像变更通知只 connect 一次（与上面的下载回显连接同款懒加载语义）。
// TcpMgr/FileTcpMgr 的 socket 都驻留主线程，handler 与本 lambda 都在主线程执行，
// 直接操作 QLabel 是安全的
void EnsureAvatarUpdateConnected()
{
    static const bool connected = []() {
        QObject::connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_avatar_updated,
            [](int uid, const QString& icon) {
                InvalidateAvatarCache(uid, icon);
            });
        return true;
    }();
    Q_UNUSED(connected);
}
} // namespace

// 自己上传新头像后的本地刷新：清自己 uid 的内存缓存并重刷注册表里的
// 自己头像 label。与 InvalidateAvatarCache 的区别——本地文件刚被
// slot_up_load 重写为新图，无需下载，重载立即生效。
// 注意：这是对外的全局函数（global.h 有声明），不能放进匿名 namespace
void RefreshSelfAvatar()
{
    int my_uid = UserMgr::GetInstance()->GetUid();
    if (my_uid <= 0) {
        return;
    }
    ClearAvatarMemCache(my_uid);

    // 上传成功后 _icon 已同步为服务器文件名；注册表里自己的条目也要改成新名字，
    // 否则重新加载仍按旧的默认编号/资源路径取图（首次换头像时就是这样）
    QString self_icon;
    if (auto self_info = UserMgr::GetInstance()->GetUserInfo()) {
        self_icon = self_info->_icon;
    }

    auto& list = PendingAvatarLabels();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->owner_uid != my_uid || !it->label) {
            continue;
        }
        if (!self_icon.isEmpty()) {
            it->icon = self_icon;
        }
        it->label->setPixmap(LoadAvatarPixmap(it->icon, it->size, it->owner_uid));
    }
}

// 头像统一加载入口：原图只解码一次（存进 QPixmapCache），缩放结果按
// "icon#所有者uid@尺寸"缓存。为什么要它：聊天列表、联系人列表、资料页、
// 搜索弹窗会对同一张头像各自 QPixmap(path) 全图解码 + SmoothTransformation 缩放，
// 条目一多主线程就卡。缓存 key 带上 owner_uid：不同用户可以同名 head.png，
// 不带的话 A 的头像下载完成后会污染 B 的缓存条目
QPixmap LoadAvatarPixmap(const QString& icon, const QSize& size, int owner_uid)
{
    // 首次调用时顺带把 1057 头像变更通知挂上（静态懒加载，重复调用零成本）：
    // 任何界面加载头像就意味着本客户端会显示别人的头像，必须监听变更
    EnsureAvatarUpdateConnected();
    // 下载完成回调也必须无条件挂上：此前只在"本地缺文件"的失败分支里挂，
    // 如果本次运行所有头像都能从本地加载（没走失败分支），1057 触发的下载
    // 完成后没有任何槽刷新 label，会话列表/联系人就会一直停在旧头像
    EnsureAvatarSignalConnected();

    // 不传 owner_uid（0）就当作"自己的头像"，用当前登录 uid
    if (owner_uid <= 0) {
        owner_uid = UserMgr::GetInstance()->GetUid();
    }

    // 最终尺寸结果缓存（ScaledCache 在匿名命名空间，InvalidateAvatarCache 共用）
    auto& scaled_cache = ScaledCache();
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
        FileTcpMgr::GetInstance()->DownloadAvatar(icon, owner_uid);
    }
    // 先显示默认头像占位；下载完成后 sig_avatar_downloaded 会把注册的 label 重刷
    QPixmap fallback = QPixmap(":/res/head_1.jpg");
    if (!size.isEmpty()) {
        fallback = fallback.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    return fallback;
}

// 把 label 注册进"待刷新表"：下载完成、或收到 1057 头像变更时统一刷新。
// 所有 icon 形态都登记（不只服务器文件名）——默认编号/资源路径的用户
// 也可能在运行期间换头像，不登记的话 1057 到达后找不到这些 label，
// 只能等重新登录才更新。QPointer 弱引用保证条目随控件销毁自动失效
QPixmap LoadAvatarPixmap(QLabel* label, const QString& icon, const QSize& size, int owner_uid)
{
    QPixmap result = LoadAvatarPixmap(icon, size, owner_uid);
    if (label && !icon.isEmpty()) {
        // 资源路径（默认头像）不归属任何用户：不能把 owner_uid=0 解析成自己，
        // 否则自己换头像时 RefreshSelfAvatar 会把这些默认图标误刷成自己的头像
        //（例如"新的朋友"入口、假申请条目）
        if (owner_uid <= 0 && !icon.startsWith(":/")) {
            owner_uid = UserMgr::GetInstance()->GetUid();
        }
        auto& list = PendingAvatarLabels();
        bool exists = false;
        for (auto it = list.begin(); it != list.end();) {
            if (!it->label) {
                it = list.erase(it);   //顺手清掉已销毁的 label
                continue;
            }
            if (it->label == label) {
                //同一个 label 只保留一条记录（控件可能被复用显示不同用户）
                it->icon = icon;
                it->size = size;
                it->owner_uid = owner_uid;
                exists = true;
                break;
            }
            ++it;
        }
        if (!exists) {
            list.append({label, icon, size, owner_uid});
        }
    }
    return result;
}

#include "userinfopage.h"
#include "usermgr.h"
#include "global.h"
#include "filetcpmgr.h"
#include "tcpmgr.h"
#include "imagecropperdialog.h"
#include "imagecropperlabel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>

/******************************************************************************
 *  userinfopage.cpp
 *  作用：用户信息页（day36 裁剪 + day39 上传链路）：
 *        1) 构造时从 UserMgr::GetUserInfo() 取 _icon 加载头像和资料（只读）
 *        2) up_btn → QFileDialog 选图 → ImageCropperDialog 圆形裁剪
 *        3) 裁剪结果缩放显示到 head_lb，PNG 保存到
 *           QStandardPaths::AppDataLocation/<uid>/avatars/head.png
 *        4) 发 1031 分块上传（FileTcpMgr::UploadHeadIcon）→ 服务端落盘
 *           output/static/<uid>/head.png 并更新 MySQL user.icon + Redis 缓存
 *        5) 同步 UserMgr 的 _icon 为服务器文件名（DB icon 语义）并发 sig_reset_head()
 *           （chatdialog 接它刷新侧边栏；收 1032 打日志确认入库）
 *****************************************************************************/
UserInfoPage::UserInfoPage(QWidget *parent)
    : QWidget(parent)
{
    InitUi();

    // 加载当前头像（默认头像或已上传的本地文件）
    LoadHeadFromLocal();

    // 用户名只读，描述可编辑；昵称编辑已移除
    QString name, desc;
    if (auto user_info = UserMgr::GetInstance()->GetUserInfo()) {
        name = user_info->_name;
        desc = user_info->_desc;
    }
    name_ed->setText(name);
    desc_ed->setText(desc);

    connect(up_btn, &QPushButton::clicked, this, &UserInfoPage::slot_up_load);
    connect(save_btn, &QPushButton::clicked, this, &UserInfoPage::slot_save_info);

    // 1022 修改资料回包：成功则更新本地 UserMgr/输入框，并通知 ChatDialog 刷气泡昵称
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_update_user_info_rsp,
        this, [this](int error, QString nick, QString desc) {
            save_btn->setEnabled(true);
            if (error != ErrorCodes::SUCCESS) {
                QMessageBox::warning(this, tr("保存失败"),
                    tr("个人资料保存失败，错误码 %1").arg(error));
                return;
            }

            desc_ed->setText(desc);
            UserMgr::GetInstance()->UpdateSelfInfo(nick, desc);
            emit sig_user_info_updated();
            QMessageBox::information(this, tr("保存成功"),
                tr("描述已更新。"));
        });

    // 头像入库确认：服务端最后一块落盘并更新 MySQL user.icon / Redis 缓存后
    // 才回的 1032（trans_size==total_size），收到即说明服务器已持久化
    connect(FileTcpMgr::GetInstance().get(), &FileTcpMgr::sig_upload_head_done,
        this, [](const QString& name) {
            qDebug() << "头像上传完成并已入库：" << name;
        });

    // 上传失败弹窗提示（此前静默吞掉 1032 的 error 回包，用户完全看不到失败原因）
    // 服务端错误码体系里 1010=token 失效、1011=uid 无效（客户端枚举未覆盖，直接按值比较）
    connect(FileTcpMgr::GetInstance().get(), &FileTcpMgr::sig_upload_head_failed,
        this, [this](int err) {
            QString tip = (err == 1010 || err == 1011)
                ? QStringLiteral("登录态已失效，请退出后重新登录再上传头像")
                : QStringLiteral("头像上传失败，错误码 %1").arg(err);
            QMessageBox::warning(this, tr("上传失败"), tip);
        });
}

// 纯代码布局（对应老师 userinfopage.ui 的结构，控件同名）：
// 标题"基本信息" → 头像+上传按钮一行 → 昵称/用户名/描述三行（只读）
void UserInfoPage::InitUi()
{
    // 外层左右留白居中（对应老师 ui 的两侧 horizontalSpacer）
    auto* outer_layout = new QHBoxLayout(this);
    outer_layout->addStretch();

    auto* content_layout = new QVBoxLayout();
    content_layout->setContentsMargins(5, 5, 5, 5);
    outer_layout->addLayout(content_layout);
    outer_layout->addStretch();

    // 标题
    auto* title_lb = new QLabel(QStringLiteral("基本信息"), this);
    content_layout->addWidget(title_lb);

    // 头像（120x120 圆形显示区）+ 上传按钮
    head_lb = new QLabel(this);
    head_lb->setFixedSize(120, 120);
    head_lb->setScaledContents(true);

    up_btn = new QPushButton(QStringLiteral("上传"), this);
    up_btn->setFixedSize(120, 30);

    auto* head_layout = new QHBoxLayout();
    head_layout->setSpacing(40);
    head_layout->addWidget(head_lb);
    head_layout->addWidget(up_btn);
    head_layout->addStretch();
    content_layout->addSpacing(40);
    content_layout->addLayout(head_layout);
    content_layout->addSpacing(20);

    // 两行资料输入框：用户名（登录名/唯一键）只读，描述可编辑。
    // 昵称编辑已按需求移除，界面显示名统一用用户名/好友备注
    auto make_row = [this, &content_layout](const QString& label_text, bool editable) {
        auto* lb = new QLabel(label_text, this);
        auto* ed = new QLineEdit(this);
        ed->setFixedSize(200, 30);
        ed->setReadOnly(!editable);
        auto* row = new QHBoxLayout();
        row->addWidget(lb);
        row->addWidget(ed);
        row->addStretch();
        content_layout->addLayout(row);
        return ed;
    };
    name_ed = make_row(QStringLiteral("用户名:"), false);
    desc_ed = make_row(QStringLiteral("描述:  "), true);

    // 保存按钮：提交昵称/描述到 ChatServer（1020/1022）
    save_btn = new QPushButton(QStringLiteral("保存"), this);
    save_btn->setFixedSize(120, 30);
    content_layout->addSpacing(12);
    content_layout->addWidget(save_btn, 0, Qt::AlignHCenter);

    content_layout->addStretch();
}

// 按统一入口解析当前头像（global.cpp 的 LoadAvatarPixmap 已兼容三种 icon 形态：
// 默认资源路径 / 老数据编号 / 服务器文件名），失败时它内部会回退默认头像，
// 文件名形态本地缺文件时还会自动触发下载并刷新 head_lb（换设备登录的场景）
void UserInfoPage::LoadHeadFromLocal()
{
    QString icon;
    if (auto user_info = UserMgr::GetInstance()->GetUserInfo()) {
        icon = user_info->_icon;
    }
    // GetUserInfo() 可能为空（未登录），icon 兜底成默认头像，保证界面不空白
    if (icon.isEmpty()) {
        icon = ":/res/head_1.jpg";
    }

    auto uid = UserMgr::GetInstance()->GetUid();
    QPixmap pixmap = LoadAvatarPixmap(head_lb, icon, head_lb->size(), uid);
    head_lb->setPixmap(MakeRoundPixmap(pixmap));
    head_lb->setScaledContents(true);
}

// 方形裁剪结果 → 圆形头像。
// 做法：新建全透明画布，用椭圆路径做裁剪区再画原图，
// 圆外像素保持透明。比 QBitmap 掩码更直观（不用记"1白显示/0黑遮掉"），
// 且带抗锯齿，边缘更平滑
QPixmap UserInfoPage::MakeRoundPixmap(const QPixmap& src)
{
    if (src.isNull()) {
        return src;
    }

    QPixmap dst(src.size());
    dst.fill(Qt::transparent);
    QPainter painter(&dst);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addEllipse(dst.rect());
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, src);
    return dst;
}

// 上传头像（老师 userinfopage.cpp slot_up_load 同名槽，接上服务器上传链路）：
// 选图 → 裁剪 → 显示 → 存本地 → 发 1031 上传 → 同步 _icon → 发 sig_reset_head()
// 保存描述：1020 请求（协议仍要求带 nick，这里原样回传当前昵称，不再允许修改）
void UserInfoPage::slot_save_info()
{
    auto user_info = UserMgr::GetInstance()->GetUserInfo();
    if (!user_info) {
        return;
    }

    save_btn->setEnabled(false);
    TcpMgr::GetInstance()->UpdateUserInfo(user_info->_nick, desc_ed->text().trimmed());
}

void UserInfoPage::slot_up_load()
{
    // 1. 选图（webp 也放行，QPixmap 装了 imageformats 插件就能解码）
    QString filename = QFileDialog::getOpenFileName(
        this,
        tr("选择图片"),
        QString(),
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)")
    );
    if (filename.isEmpty()) {
        return;
    }

    // 2. 弹模态裁剪框：600x400 预览窗口、圆形裁剪框（拖角等比缩放）
    QPixmap image = ImageCropperDialog::getCroppedImage(filename, 600, 400, CropperShape::CIRCLE);
    if (image.isNull()) {
        return;   // 加载失败或用户点了取消
    }

    // 3. 圆形遮罩后缩放到 head_lb 大小显示
    QPixmap round_pixmap = MakeRoundPixmap(image);
    QPixmap scaled_pixmap = round_pixmap.scaled(head_lb->size(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    head_lb->setPixmap(scaled_pixmap);
    head_lb->setScaledContents(true);

    // 4. 保存到 AppData/<uid>/avatars/head.png（目录不存在就 mkpath）
    QString storage_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    auto uid = UserMgr::GetInstance()->GetUid();
    QDir dir(storage_dir + "/" + QString::number(uid));
    if (!dir.exists("avatars") && !dir.mkpath("avatars")) {
        qWarning() << "无法创建 avatars 目录：" << dir.filePath("avatars");
        QMessageBox::warning(this, tr("错误"),
            tr("无法创建存储目录，请检查权限或磁盘空间。"));
        return;
    }
    QString file_path = dir.filePath("avatars/head.png");
    // 存裁剪原图（不存缩放图）：清晰度最好，上传服务器也直接用这份
    if (!round_pixmap.save(file_path, "PNG")) {
        QMessageBox::warning(this, tr("保存失败"),
            tr("头像保存失败，请检查权限或磁盘空间。"));
        return;
    }
    qDebug() << "头像已保存到：" << file_path;

    // 4.5 先同步 UserMgr 里当前用户的 _icon 为服务器文件名（老师语义：DB icon 存文件名）：
    //     本地回显时 LoadHeadFromLocal 会用文件名拼回 AppData/<uid>/avatars/ 目录。
    //     必须在 RefreshSelfAvatar 之前——否则注册表里的自己头像仍会被改成旧编号
    //     （首次上传时 _icon 还是 "1"），重载后还是旧图
    if (auto user_info = UserMgr::GetInstance()->GetUserInfo()) {
        user_info->_icon = QFileInfo(file_path).fileName();
    }

    // 本地文件已重写为新图：清自己 uid 的内存缓存并重刷所有显示自己头像的
    // label（侧边栏/资料页/气泡/列表等，经 QPointer 注册表统一处理）
    RefreshSelfAvatar();

    // 5. 发 1031 上传到资源服务器（异步）：服务端按 <uid>/head.png 落盘到
    //    output/static/，最后一块落盘后更新 MySQL user.icon + Redis 缓存再回 1032。
    //    未连接资源服务器时静默失败（本地头像已保存，界面显示不受影响）
    FileTcpMgr::GetInstance()->UploadHeadIcon(file_path);

    // 6. 通知外层（chatdialog）刷新侧边栏头像
    emit sig_reset_head();
}

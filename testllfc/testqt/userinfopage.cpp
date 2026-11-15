#include "userinfopage.h"
#include "usermgr.h"
#include "global.h"
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
 *  作用：用户信息页（day36）。本轮只做"看 + 换本地头像"：
 *        1) 构造时从 UserMgr::GetUserInfo() 取 _icon 加载头像和资料（只读）
 *        2) up_btn → QFileDialog 选图 → ImageCropperDialog 圆形裁剪
 *        3) 裁剪结果缩放显示到 head_lb，PNG 保存到
 *           QStandardPaths::AppDataLocation/<uid>/avatars/head.png
 *        4) 同步 UserMgr 的 _icon 为本地路径并发 sig_reset_head()
 *           （chatdialog 接它刷新侧边栏；服务器上传是后续轮次）
 *****************************************************************************/
UserInfoPage::UserInfoPage(QWidget *parent)
    : QWidget(parent)
{
    InitUi();

    // 加载当前头像（默认头像或已上传的本地文件）
    LoadHeadFromLocal();

    // 本轮资料只读展示：从 UserMgr 取值填充，拿不到（未登录/空指针）就留空兜底
    QString name, nick, desc;
    if (auto user_info = UserMgr::GetInstance()->GetUserInfo()) {
        name = user_info->_name;
        nick = user_info->_nick;
        desc = user_info->_desc;
    }
    name_ed->setText(name);
    nick_ed->setText(nick);
    desc_ed->setText(desc);

    connect(up_btn, &QPushButton::clicked, this, &UserInfoPage::slot_up_load);
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

    // 三行资料输入框（只读 QLineEdit：既和老师 ui 的控件类型一致，
    // 又明确告诉用户"这里当前不能改"，不用假按钮制造可提交的错觉）
    auto make_row = [this, &content_layout](const QString& label_text) {
        auto* lb = new QLabel(label_text, this);
        auto* ed = new QLineEdit(this);
        ed->setFixedSize(200, 30);
        ed->setReadOnly(true);
        auto* row = new QHBoxLayout();
        row->addWidget(lb);
        row->addWidget(ed);
        row->addStretch();
        content_layout->addLayout(row);
        return ed;
    };
    nick_ed = make_row(QStringLiteral("昵称： "));
    name_ed = make_row(QStringLiteral("用户名:"));
    desc_ed = make_row(QStringLiteral("描述:  "));

    content_layout->addStretch();
}

// 按老师逻辑加载当前头像：
// - ":/res/head_X.jpg" 是随包默认头像，直接当资源加载
// - 其他值是本地已保存的头像文件，从 AppData/<uid>/avatars/ 目录读
// 统一走 LoadAvatarPixmap（解码+缩放都有缓存），最后套圆形遮罩显示
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

    // 用正则判断是不是默认头像（":/res/head_数字.jpg"）
    QRegularExpression regex("^:/res/head_(\\d+)\\.jpg$");
    QPixmap pixmap;
    if (regex.match(icon).hasMatch()) {
        // 默认头像：资源路径直接加载
        pixmap = LoadAvatarPixmap(icon, head_lb->size());
    }
    else {
        // 用户自己的头像：从 AppData/<uid>/avatars/ 读本地文件
        QString storage_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        auto uid = UserMgr::GetInstance()->GetUid();
        QString avatar_path = storage_dir + "/" + QString::number(uid)
                              + "/avatars/" + QFileInfo(icon).fileName();
        pixmap = LoadAvatarPixmap(avatar_path, head_lb->size());
        if (pixmap.isNull()) {
            // 本地文件丢了（换机器/清缓存）就回退默认头像
            qWarning() << "无法加载本地上传的头像：" << avatar_path;
            pixmap = LoadAvatarPixmap(":/res/head_1.jpg", head_lb->size());
        }
    }

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

// 上传头像（老师 userinfopage.cpp 同名槽，去掉服务器上传部分）：
// 选图 → 裁剪 → 显示 → 存本地 → 同步 _icon → 发 sig_reset_head()
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
    // 存裁剪原图（不存缩放图）：清晰度最好，后续轮次上传服务器也直接用它
    if (!round_pixmap.save(file_path, "PNG")) {
        QMessageBox::warning(this, tr("保存失败"),
            tr("头像保存失败，请检查权限或磁盘空间。"));
        return;
    }
    qDebug() << "头像已保存到：" << file_path;

    // 5. 同步 UserMgr 里当前用户的 _icon 为本地路径：
    //    这样其他界面（侧边栏等）刷新头像时拿到的是同一份
    if (auto user_info = UserMgr::GetInstance()->GetUserInfo()) {
        user_info->_icon = file_path;
    }

    // 6. 通知外层（chatdialog）刷新侧边栏头像
    emit sig_reset_head();
}

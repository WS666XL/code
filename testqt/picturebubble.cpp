#include "picturebubble.h"
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QDialog>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>


#define PIC_MAX_WIDTH 160
#define PIC_MAX_HEIGHT 90

PictureBubble::PictureBubble(const QPixmap &picture, ChatRole role, QWidget *parent)
    :BubbleFrame(role, parent)
{
    _pic_lb = new QLabel();
    // 不要 stretch：图片按 QPixmap 自身尺寸显示，气泡大小也按它算，
    // 否则接收方 160×90 的占位气泡会把竖图拉宽，和发送方尺寸对不上
    _pic_lb->setScaledContents(false);
    // 让点击穿透到气泡本身：传输中点击图片是"暂停/继续"（见 mousePressEvent）
    _pic_lb->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QPixmap pix = picture.scaled(QSize(PIC_MAX_WIDTH, PIC_MAX_HEIGHT), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    _pic_lb->setPixmap(pix);
    _pic_lb->setFixedSize(pix.size());
    _pic_size = pix.size();

    // 上传进度条：放在图片下方，没有上传任务时隐藏（历史消息/接收方气泡不显示）
    _progress_bar = new QProgressBar();
    _progress_bar->setRange(0, 100);
    _progress_bar->setValue(0);
    _progress_bar->setTextVisible(true);
    _progress_bar->setFormat(QStringLiteral("上传中 %1%"));
    _progress_bar->setFixedHeight(14);
    _progress_bar->setStyleSheet(
        "QProgressBar{border:none;background:rgba(0,0,0,40);border-radius:3px;"
        "color:#333;font-size:10px;}"
        "QProgressBar::chunk{background:#3a9d5d;border-radius:3px;}");
    _progress_bar->hide();

    // 图片和进度条装进一个竖排容器，再交给 BubbleFrame 的气泡布局
    auto* container = new QWidget();
    auto* v_layout = new QVBoxLayout(container);
    v_layout->setContentsMargins(0, 0, 0, 0);
    v_layout->setSpacing(4);
    v_layout->addWidget(_pic_lb);
    v_layout->addWidget(_progress_bar);
    this->setWidget(container);

    adjustBubbleSize();
}

//图片下载完成后刷新气泡：按真图等比缩放并重算气泡尺寸。
//发送方也用真图构造，所以两边最终尺寸一致（占位阶段会短暂显示 160×90）
void PictureBubble::setPicture(const QPixmap& picture)
{
    QPixmap pix = picture.scaled(QSize(PIC_MAX_WIDTH, PIC_MAX_HEIGHT),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    _pic_lb->setPixmap(pix);
    // 下载完成拿到真图后，按真图的等比尺寸重算气泡大小：
    // 接收方从 160×90 占位气泡收缩到和发送方一致的尺寸
    _pic_lb->setFixedSize(pix.size());
    _pic_size = pix.size();
    adjustBubbleSize();
}

//按"图片尺寸 + 气泡边距 (+ 进度条高度)"重算固定尺寸。
//为什么不用自动布局：图片气泡原本就是固定大小，进度条显示/隐藏时
//只增高/还原这一条，聊天气泡才不会因为进度文字变化而抖动
void PictureBubble::adjustBubbleSize()
{
    int left_margin = this->layout()->contentsMargins().left();
    int right_margin = this->layout()->contentsMargins().right();
    int v_margin = this->layout()->contentsMargins().bottom();

    int width = _pic_size.width() + left_margin + right_margin;
    int height = _pic_size.height() + v_margin * 2;
    if (_progress_bar->isVisible()) {
        height += _progress_bar->height() + 4;   // 4 = 容器 layout 的 spacing
    }
    setFixedSize(width, height);
}

//进度：1038（上传）/1034（下载）每确认一块由 ChatPage 转进来。
//进度条首次出现时增高气泡；到 100% 延迟 0.8 秒隐藏，避免"一闪而过"
void PictureBubble::setProgress(qint64 value, qint64 total)
{
    if (total <= 0) {
        return;
    }

    int percent = int(value * 100 / total);
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    _percent = percent;

    bool was_hidden = _progress_bar->isHidden();
    _progress_bar->setValue(percent);
    if (was_hidden) {
        _progress_bar->show();
        adjustBubbleSize();
    }
    refreshProgressText();

    if (percent >= 100) {
        setTransferState(TransferState::Completed);
        QTimer::singleShot(800, this, [this]() {
            _progress_bar->hide();
            adjustBubbleSize();
        });
    }
}

void PictureBubble::setTransferContext(const QString& name, bool is_upload)
{
    _unique_name = name;
    _is_upload = is_upload;
}

//切换传输状态：进度条显示规则全在这里，进度数字/文字由 refreshProgressText 更新
void PictureBubble::setTransferState(TransferState state)
{
    _state = state;
    switch (state) {
    case TransferState::Uploading:
    case TransferState::Downloading:
    case TransferState::Paused:
        if (_progress_bar->isHidden()) {
            _progress_bar->show();
            adjustBubbleSize();
        }
        break;
    case TransferState::Completed:
        // 完成先让用户看到 100%，0.8 秒后再收起
        QTimer::singleShot(800, this, [this]() {
            _progress_bar->hide();
            adjustBubbleSize();
        });
        break;
    case TransferState::Failed:
    case TransferState::None:
        _progress_bar->hide();
        adjustBubbleSize();
        break;
    }
    refreshProgressText();
}

//进度条文字随方向和暂停状态变化；暂停时提示"点图继续"（交互与老师版一致）
void PictureBubble::refreshProgressText()
{
    if (_state == TransferState::Paused) {
        _progress_bar->setFormat(QStringLiteral("已暂停 %1% · 点图继续").arg(_percent));
    }
    else if (_is_upload) {
        _progress_bar->setFormat(QStringLiteral("上传中 %1%").arg(_percent));
    }
    else {
        _progress_bar->setFormat(QStringLiteral("下载中 %1%").arg(_percent));
    }
}

//传输中点击图片切换暂停/继续；传输完成后单击打开全屏预览
void PictureBubble::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !_unique_name.isEmpty()) {
        if (_state == TransferState::Uploading || _state == TransferState::Downloading) {
            setTransferState(TransferState::Paused);
            emit sig_transfer_pause(_unique_name, _is_upload);
            return;
        }
        if (_state == TransferState::Paused) {
            // 先乐观切回传输中；服务器同步失败时 ChatPage 会再切 Failed
            setTransferState(_is_upload ? TransferState::Uploading
                                        : TransferState::Downloading);
            emit sig_transfer_resume(_unique_name, _is_upload);
            return;
        }
    }

    // 已完成/空闲：单击预览（本地文件存在时才可用）
    if (event->button() == Qt::LeftButton && !_local_path.isEmpty()
        && QFileInfo::exists(_local_path)
        && (_state == TransferState::Completed || _state == TransferState::None)) {
        ShowPreview();
        return;
    }
    BubbleFrame::mousePressEvent(event);
}

//全屏预览：黑底、图片等比缩放到屏幕大小，顶部提供"另存为"和"关闭"
void PictureBubble::ShowPreview()
{
    if (_local_path.isEmpty() || !QFileInfo::exists(_local_path)) {
        return;
    }

    QDialog preview(this);
    preview.setWindowTitle(QStringLiteral("图片预览"));
    preview.setStyleSheet("QDialog{background:#000000;}");

    auto* layout = new QVBoxLayout(&preview);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* bar = new QHBoxLayout();
    bar->addStretch();
    auto* save_btn = new QPushButton(QStringLiteral("另存为"), &preview);
    auto* close_btn = new QPushButton(QStringLiteral("关闭"), &preview);
    const QString btn_style =
        "QPushButton{background:#333;color:#fff;border:1px solid #555;"
        "border-radius:4px;padding:5px 14px;}"
        "QPushButton:hover{background:#444;}";
    save_btn->setStyleSheet(btn_style);
    close_btn->setStyleSheet(btn_style);
    bar->addWidget(save_btn);
    bar->addWidget(close_btn);
    layout->addLayout(bar);

    auto* image_lb = new QLabel(&preview);
    image_lb->setAlignment(Qt::AlignCenter);
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    const QSize avail = screen ? screen->availableGeometry().size() : QSize(1280, 720);
    QPixmap pix(_local_path);
    if (!pix.isNull()) {
        // 预览最大占屏幕宽高的一半，等比缩放：
        // 大图不会一打开就铺满屏幕，小图也会放大到合适尺寸
        const QSize target(int(avail.width() * 0.5), int(avail.height() * 0.5));
        image_lb->setPixmap(pix.scaled(
            target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    layout->addWidget(image_lb, 1);

    connect(close_btn, &QPushButton::clicked, &preview, &QDialog::reject);
    connect(save_btn, &QPushButton::clicked, &preview, [this, &preview]() {
        const QString default_name = QFileInfo(_local_path).fileName();
        const QString path = QFileDialog::getSaveFileName(
            &preview, QStringLiteral("另存为"),
            QDir::homePath() + "/" + default_name,
            QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (path.isEmpty()) {
            return;
        }
        // 选的是原文件本身：直接当作保存成功，不能先 remove 再 copy 把自己删了
        if (QFileInfo(path).absoluteFilePath()
            == QFileInfo(_local_path).absoluteFilePath()) {
            return;
        }
        if (QFile::exists(path)) {
            QFile::remove(path);
        }
        // 优先直接复制原始文件（不重新编码）；失败再退回 QPixmap::save
        bool ok = QFile::copy(_local_path, path);
        if (!ok) {
            QPixmap save_pix(_local_path);
            ok = !save_pix.isNull() && save_pix.save(path);
        }
        if (!ok) {
            QMessageBox::warning(&preview, QStringLiteral("保存失败"),
                QStringLiteral("图片另存为失败，请检查目标路径权限。"));
        }
    });

    // 不再全屏显示：窗口按图片尺寸自适应，居中弹出
    preview.adjustSize();
    preview.exec();
}

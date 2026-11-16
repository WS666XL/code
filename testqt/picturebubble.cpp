#include "picturebubble.h"
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QMouseEvent>


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

//传输中点击图片切换暂停/继续；其他状态不拦截点击（保持原有行为）
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
    BubbleFrame::mousePressEvent(event);
}

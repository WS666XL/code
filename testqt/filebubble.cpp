#include "filebubble.h"
#include <QMouseEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QDebug>

//字节数格式化成 "12.3 KB / 3.4 MB"（文件卡片状态行显示用）
static QString formatSize(qint64 total_size)
{
    if (total_size >= 1024 * 1024) {
        return QString::number(total_size / (1024.0 * 1024.0), 'f', 1) + " MB";
    }
    return QString::number(total_size / 1024.0, 'f', 1) + " KB";
}

FileBubble::FileBubble(const QString& file_name, qint64 total_size, ChatRole role,
                       QWidget *parent)
    : BubbleFrame(role, parent),
      _file_name(file_name),
      _total_size(total_size)
{
    //卡片容器：左侧文件图标 + 右侧两行文字（文件名 / 大小·状态）
    QWidget* card = new QWidget();
    auto* h_layout = new QHBoxLayout(card);
    h_layout->setContentsMargins(10, 8, 10, 8);
    h_layout->setSpacing(10);

    //文件图标：用 Qt 内置的"文件"图标（跨平台稳定，不用额外资源）
    _icon_lb = new QLabel();
    _icon_lb->setPixmap(style()->standardIcon(QStyle::SP_FileIcon).pixmap(32, 32));
    _icon_lb->setFixedSize(32, 32);
    _icon_lb->setScaledContents(true);
    h_layout->addWidget(_icon_lb);

    auto* v_layout = new QVBoxLayout();
    v_layout->setContentsMargins(0, 0, 0, 0);
    v_layout->setSpacing(4);
    h_layout->addLayout(v_layout);

    //文件名：过长时中间截断（保留扩展名方便辨认）
    _name_lb = new QLabel(_file_name);
    _name_lb->setStyleSheet("color:#222; font-size:13px;");
    if (_file_name.length() > 28) {
        _name_lb->setText(_file_name.left(20) + "..." + _file_name.right(6));
    }
    _name_lb->setToolTip(_file_name);   //悬停看完整文件名
    v_layout->addWidget(_name_lb);

    _status_lb = new QLabel();
    _status_lb->setStyleSheet("color:#999; font-size:11px;");
    v_layout->addWidget(_status_lb);

    //卡片固定宽度（文件名宽度变化时气泡不至于抖动）
    card->setFixedWidth(240);
    this->setWidget(card);
    refreshStatus();
}

//按本地路径是否存在刷新状态文字：
//无文件 → "点击下载"；有文件 → "点击打开"（发送方构造时本地就在，直接是可打开态）
void FileBubble::refreshStatus()
{
    QString size_str = formatSize(_total_size);

    if (_local_path.isEmpty()) {
        _status_lb->setText(size_str + " · 点击下载");
    }
    else {
        _status_lb->setText(size_str + " · 已下载，点击打开");
    }
}

void FileBubble::setLocalPath(const QString& local_path)
{
    _local_path = local_path;
    refreshStatus();
}

void FileBubble::setDownloading()
{
    _status_lb->setText(formatSize(_total_size) + " · 下载中...");
}

void FileBubble::setDownloadFailed()
{
    _local_path.clear();
    refreshStatus();
}

void FileBubble::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        //点击语义：已下载 → 用系统默认程序打开文件；未下载 → 交给外部发起下载
        if (!_local_path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(_local_path));
        }
        else {
            emit sig_card_clicked();
        }
        return;
    }
    BubbleFrame::mousePressEvent(event);
}

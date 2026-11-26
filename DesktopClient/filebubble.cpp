#include "filebubble.h"
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QUrl>
#include <QTimer>
#include <QProgressBar>
#include <QToolButton>
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

    //上传进度条：默认隐藏，发送方开始上传后由 setProgress 显示
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
    v_layout->addWidget(_progress_bar);

    //暂停/继续按钮：只在传输中显示（day38 断点续传）。
    //用扁平 QToolButton，尽量不破坏文件卡片原有观感
    _pause_btn = new QToolButton();
    _pause_btn->setText(QStringLiteral("暂停"));
    _pause_btn->setCursor(Qt::PointingHandCursor);
    _pause_btn->setAutoRaise(true);
    _pause_btn->setStyleSheet("QToolButton{color:#3a9d5d;font-size:11px;border:none;}"
                              "QToolButton:hover{text-decoration:underline;}");
    _pause_btn->hide();
    v_layout->addWidget(_pause_btn, 0, Qt::AlignLeft);
    connect(_pause_btn, &QToolButton::clicked, this, &FileBubble::onPauseClicked);

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
    setTransferState(TransferState::Completed);
    refreshStatus();
}

void FileBubble::setDownloading()
{
    setTransferState(TransferState::Downloading);
    _status_lb->setText(formatSize(_total_size) + " · 下载中...");
}

void FileBubble::setDownloadFailed()
{
    _local_path.clear();
    setTransferState(TransferState::Failed);
    refreshStatus();
}

//进度：卡片高度随进度条/按钮显示自动增长（容器是布局管理）。
//到 100% 后延迟隐藏，让用户看到"完成"再收起
void FileBubble::setProgress(qint64 value, qint64 total)
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

    _progress_bar->setValue(percent);
    if (_progress_bar->isHidden()) {
        _progress_bar->show();
        _pause_btn->show();
    }
    refreshProgressText();

    if (percent >= 100) {
        setTransferState(TransferState::Completed);
    }
}

void FileBubble::setTransferContext(const QString& name, bool is_upload)
{
    _unique_name = name;
    _is_upload = is_upload;
}

//状态切换统一管进度条/暂停按钮的显隐：
//传输中（含暂停）显示；完成/失败/空闲隐藏
void FileBubble::setTransferState(TransferState state)
{
    _state = state;
    switch (state) {
    case TransferState::Uploading:
    case TransferState::Downloading:
    case TransferState::Paused:
        _progress_bar->show();
        _pause_btn->show();
        _pause_btn->setText(state == TransferState::Paused
                                ? QStringLiteral("继续") : QStringLiteral("暂停"));
        refreshProgressText();
        break;
    case TransferState::Completed:
        // 完成保留 0.8 秒进度条，让用户看到 100%
        QTimer::singleShot(800, this, [this]() {
            _progress_bar->hide();
            _pause_btn->hide();
        });
        break;
    case TransferState::Failed:
        _progress_bar->hide();
        if (_is_upload && !_unique_name.isEmpty()) {
            // 上传失败：保留一个“重试”入口，用户不用重新选文件
            _pause_btn->setText(QStringLiteral("重试"));
            _pause_btn->show();
        }
        else {
            _pause_btn->hide();
        }
        break;
    case TransferState::None:
        _progress_bar->hide();
        _pause_btn->hide();
        break;
    }
}

void FileBubble::refreshProgressText()
{
    if (_state == TransferState::Paused) {
        _progress_bar->setFormat(QStringLiteral("已暂停 %1%").arg(_percent));
    }
    else if (_is_upload) {
        _progress_bar->setFormat(QStringLiteral("上传中 %1%").arg(_percent));
    }
    else {
        _progress_bar->setFormat(QStringLiteral("下载中 %1%").arg(_percent));
    }
}

//按钮点击：传输中 → 暂停；已暂停 → 继续；上传失败 → 重试
void FileBubble::onPauseClicked()
{
    if (_state == TransferState::Failed && _is_upload) {
        emit sig_transfer_retry(_unique_name);
        return;
    }
    if (_state == TransferState::Uploading || _state == TransferState::Downloading) {
        setTransferState(TransferState::Paused);
        emit sig_transfer_pause(_unique_name, _is_upload);
        return;
    }
    if (_state == TransferState::Paused) {
        setTransferState(_is_upload ? TransferState::Uploading
                                    : TransferState::Downloading);
        emit sig_transfer_resume(_unique_name, _is_upload);
    }
}

void FileBubble::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        //点击语义：已下载 → 用系统默认程序打开；未下载 → 交给外部发起下载
        if (!_local_path.isEmpty()) {
            if (!QFileInfo::exists(_local_path)) {
                //文件被用户移动/删除：清掉失效路径，提示重新下载
                QMessageBox::information(this, QStringLiteral("提示"),
                    QStringLiteral("文件已被移动或删除，请右键选择“重新下载”。"));
                _local_path.clear();
                refreshStatus();
                return;
            }
            OpenLocalFile();
        }
        else {
            emit sig_card_clicked();
        }
        return;
    }
    BubbleFrame::mousePressEvent(event);
}

//右键菜单：文件操作 + 删除消息（删除仍走基类信号，交给 ChatPage 处理）
void FileBubble::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    bool exists = !_local_path.isEmpty() && QFileInfo::exists(_local_path);

    if (exists) {
        menu.addAction(QStringLiteral("打开"), this, &FileBubble::OpenLocalFile);
        menu.addAction(QStringLiteral("另存为..."), this, &FileBubble::SaveAs);
        menu.addAction(QStringLiteral("打开所在文件夹"), this, &FileBubble::OpenContainingFolder);
    }
    else {
        menu.addAction(QStringLiteral("下载"), this, &FileBubble::Redownload);
    }

    if (_state == TransferState::Failed && _is_upload) {
        menu.addAction(QStringLiteral("重新上传"), this, [this]() {
            emit sig_transfer_retry(_unique_name);
        });
    }

    menu.addSeparator();
    menu.addAction(QStringLiteral("删除消息"), this, [this]() {
        emit sig_delete_requested();
    });

    menu.exec(event->globalPos());
    event->accept();
}

void FileBubble::OpenLocalFile()
{
    if (_local_path.isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(_local_path));
}

void FileBubble::SaveAs()
{
    if (_local_path.isEmpty() || !QFileInfo::exists(_local_path)) {
        return;
    }
    QString default_name = _file_name.isEmpty() ? QFileInfo(_local_path).fileName()
                                                : _file_name;
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("另存为"),
        QDir::homePath() + "/" + default_name);
    if (path.isEmpty()) {
        return;
    }
    // 选的就是原文件本身：直接当成功，不能先 remove 再 copy 把自己删掉
    if (QFileInfo(path).absoluteFilePath()
        == QFileInfo(_local_path).absoluteFilePath()) {
        return;
    }
    if (QFile::exists(path) && !QFile::remove(path)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
            QStringLiteral("目标文件无法覆盖，请换一个位置。"));
        return;
    }
    if (!QFile::copy(_local_path, path)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
            QStringLiteral("复制文件失败，请检查目标路径权限。"));
    }
}

void FileBubble::OpenContainingFolder()
{
    if (_local_path.isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(_local_path).absolutePath()));
}

void FileBubble::Redownload()
{
    //发送时创建的气泡没有接下载回调：原文件不在本机时提示重新发送，
    //避免卡片一直停在“下载中”
    if (receivers(SIGNAL(sig_card_clicked())) <= 0) {
        setTransferState(TransferState::None);
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("原文件已不在本机，请重新选择文件发送。"));
        return;
    }
    _local_path.clear();
    setTransferState(TransferState::Downloading);
    refreshStatus();
    emit sig_card_clicked();
}

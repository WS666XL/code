#ifndef FILEBUBBLE_H
#define FILEBUBBLE_H

#include "bubbleframe.h"
#include <QLabel>
#include <QProgressBar>

class QToolButton;

/******************************************************************************
 *  文件：filebubble.h
 *  作用：文件消息的气泡——一张"文件卡片"（文件图标 + 文件名 + 大小/状态）。
 *  交互：接收方本地没有文件时显示"点击下载"，点卡片发起 1033 下载；
 *        下载完成 setLocalPath() 后显示"点击打开"，再点用系统默认程序打开文件。
 *        发送方本地文件还在，直接显示"点击打开"。
 *  day41：文件聊天与图片聊天共用 1035-1039 链路（老师版未实现文件消息，
 *         这是当前版的自研扩展），气泡不同但传输层完全复用
 *****************************************************************************/
class FileBubble : public BubbleFrame
{
    Q_OBJECT
public:
    //file_name 原始文件名（卡片显示）；total_size 字节数（格式化成 xx KB 显示）
    FileBubble(const QString& file_name, qint64 total_size, ChatRole role,
               QWidget *parent = nullptr);

    //下载完成：记录本地路径，状态从"点击下载"切换为"点击打开"
    void setLocalPath(const QString& local_path);
    //下载中：状态文字切换为"下载中..."（失败后可重新点击）
    void setDownloading();
    //下载失败：状态提示回"点击下载"（可重试）
    void setDownloadFailed();
    //进度（day40/day38）：value/total 为已确认字节数/总字节数；
    //上传/下载期间显示进度条，100% 后延迟隐藏
    void setProgress(qint64 value, qint64 total);
    //传输上下文：unique_name 用于暂停/继续时找回上传任务或下载上下文
    void setTransferContext(const QString& name, bool is_upload);
    //切换传输状态：控制进度条+暂停按钮的显示与文字
    void setTransferState(TransferState state);

signals:
    void sig_card_clicked();   //卡片被点击：外部决定是发起下载还是打开文件
    void sig_transfer_pause(const QString& name, bool is_upload);
    void sig_transfer_resume(const QString& name, bool is_upload);

protected:
    //整张卡片可点：在容器上捕获鼠标点击（QLabel 不接收点击）
    void mousePressEvent(QMouseEvent* event) override;

private:
    void refreshStatus();      //按 _local_path 是否存在刷新状态文字
    void refreshProgressText();
    void onPauseClicked();     //暂停/继续按钮

    QLabel* _icon_lb;          //左侧文件图标（QSS 画的纸张图标或内置图标）
    QLabel* _name_lb;          //文件名（过长自动截断中间省略）
    QLabel* _status_lb;        //大小 + 状态（"12.3 KB · 点击下载"）
    QProgressBar* _progress_bar;  //上传/下载进度条
    QToolButton* _pause_btn;      //暂停/继续（仅在传输中显示）
    QString _local_path;       //本地文件路径（下载完成后非空）
    QString _file_name;        //原始文件名
    QString _unique_name;      //服务器唯一文件名（暂停/继续用）
    bool _is_upload = false;   //当前气泡在传的是上传还是下载
    TransferState _state = TransferState::None;
    int _percent = 0;
    qint64  _total_size;       //文件字节数
};
#endif // FILEBUBBLE_H

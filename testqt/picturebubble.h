#ifndef PICTUREBUBBLE_H
#define PICTUREBUBBLE_H

#include "bubbleframe.h"
#include <QHBoxLayout>
#include <QPixmap>
#include <QLabel>
#include <QProgressBar>

/******************************************************************************
 *  文件：picturebubble.h
 *  作用：图片消息的气泡（气泡里装的是一张 QLabel 显示的图片）
 *  day39：图片可能本地还没有（接收方/历史记录加载），此时先显示占位图，
 *         下载完成后外部调 setPicture() 把真图换上去
 *  day40：自己发图时在图片下方显示上传进度条（1038 回执驱动）
 *****************************************************************************/
class PictureBubble : public BubbleFrame//图片气泡
{
    Q_OBJECT
public:
    //picture 是要显示的图片；role 决定气泡靠左还是靠右
    PictureBubble(const QPixmap &picture, ChatRole role, QWidget *parent = nullptr);
    //下载完成 / 本地图片就绪后刷新气泡内容（按气泡尺寸重新缩放）
    void setPicture(const QPixmap& picture);
    //进度：value/total 为已确认字节数/总字节数；达到 100% 后延迟隐藏进度条。
    //上传/下载共用，按钮文字由 setTransferState 决定
    void setProgress(qint64 value, qint64 total);
    //传输上下文：unique_name 用于暂停/继续时找回上传任务或下载上下文
    void setTransferContext(const QString& name, bool is_upload);
    //记录图片本地路径：传输完成后单击图片可全屏预览、另存为
    void setLocalPath(const QString& path) { _local_path = path; }
    //按状态刷新进度条（上传中/下载中/已暂停/完成/失败）
    void setTransferState(TransferState state);

signals:
    //点图片切换暂停/继续：ChatPage 根据 is_upload 调 FileTcpMgr 对应接口
    void sig_transfer_pause(const QString& name, bool is_upload);
    void sig_transfer_resume(const QString& name, bool is_upload);

protected:
    //图片本身是"暂停/继续"的开关（老师版同款交互：传输中点击图片切换）
    void mousePressEvent(QMouseEvent* event) override;

private:
    //进度条显示/隐藏后气泡高度会变，统一在这里重算固定尺寸
    void adjustBubbleSize();
    void refreshProgressText();
    void ShowPreview();   //全屏预览 + 另存为

    QLabel* _pic_lb;
    QProgressBar* _progress_bar;   //上传/下载进度条（默认隐藏）
    QSize _pic_size;               //图片显示尺寸（构造时确定，避免气泡跳动）
    QString _unique_name;          //服务器唯一文件名（暂停/继续用）
    QString _local_path;           //本地图片路径（预览/另存为用）
    bool _is_upload = false;       //当前气泡在传的是上传还是下载
    TransferState _state = TransferState::None;
    int _percent = 0;
};
#endif // PICTUREBUBBLE_H

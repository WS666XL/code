#ifndef PICTUREBUBBLE_H
#define PICTUREBUBBLE_H

#include "bubbleframe.h"
#include <QHBoxLayout>
#include <QPixmap>
#include <QLabel>

/******************************************************************************
 *  文件：picturebubble.h
 *  作用：图片消息的气泡（气泡里装的是一张 QLabel 显示的图片）
 *  day39：图片可能本地还没有（接收方/历史记录加载），此时先显示占位图，
 *         下载完成后外部调 setPicture() 把真图换上去
 *****************************************************************************/
class PictureBubble : public BubbleFrame//图片气泡
{
public:
    //picture 是要显示的图片；role 决定气泡靠左还是靠右
    PictureBubble(const QPixmap &picture, ChatRole role, QWidget *parent = nullptr);
    //下载完成 / 本地图片就绪后刷新气泡内容（按气泡尺寸重新缩放）
    void setPicture(const QPixmap& picture);
private:
    QLabel* _pic_lb;
};
#endif // PICTUREBUBBLE_H

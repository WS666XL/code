#ifndef PICTUREBUBBLE_H
#define PICTUREBUBBLE_H

#include "BubbleFrame.h"
#include <QHBoxLayout>
#include <QPixmap>

/******************************************************************************
 *  文件：picturebubble.h
 *  作用：图片消息的气泡（气泡里装的是一张 QLabel 显示的图片）
 *****************************************************************************/
class PictureBubble : public BubbleFrame//图片气泡
{
    Q_OBJECT
public:
    //picture 是要显示的图片；role 决定气泡靠左还是靠右
    PictureBubble(const QPixmap &picture, ChatRole role, QWidget *parent = nullptr);
};

#endif // PICTUREBUBBLE_H
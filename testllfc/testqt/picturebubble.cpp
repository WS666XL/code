#include "picturebubble.h"
#include <QLabel>


#define PIC_MAX_WIDTH 160
#define PIC_MAX_HEIGHT 90

PictureBubble::PictureBubble(const QPixmap &picture, ChatRole role, QWidget *parent)
    :BubbleFrame(role, parent)
{
    _pic_lb = new QLabel();
    _pic_lb->setScaledContents(true);
    QPixmap pix = picture.scaled(QSize(PIC_MAX_WIDTH, PIC_MAX_HEIGHT), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    _pic_lb->setPixmap(pix);
    this->setWidget(_pic_lb);

    int left_margin = this->layout()->contentsMargins().left();
    int right_margin = this->layout()->contentsMargins().right();
    int v_margin = this->layout()->contentsMargins().bottom();
    setFixedSize(pix.width()+left_margin + right_margin, pix.height() + v_margin *2);
}

//图片下载完成后刷新气泡：按气泡尺寸重新缩放真图并替换占位图。
//尺寸固定为构造时的占位尺寸，避免气泡在聊天列表里跳动
void PictureBubble::setPicture(const QPixmap& picture)
{
    QPixmap pix = picture.scaled(QSize(PIC_MAX_WIDTH, PIC_MAX_HEIGHT),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    _pic_lb->setPixmap(pix);
}

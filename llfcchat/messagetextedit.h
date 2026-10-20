#ifndef MESSAGETEXTEDIT_H
#define MESSAGETEXTEDIT_H

#include <QObject>
#include <QTextEdit>
#include <QMouseEvent>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMimeType>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QPainter>
#include <QVector>
#include "global.h"


class MessageTextEdit : public QTextEdit//消息文本编辑
{
    Q_OBJECT
public:
    explicit MessageTextEdit(QWidget *parent = nullptr);

    ~MessageTextEdit();

    QVector<MsgInfo> getMsgList();//取出输入框里的所有消息（发出去时调用）

    void insertFileFromUrl(const QStringList &urls);  // 拖入文件时：按类型分派到"插图片"或"插文本文件"
signals:
    void send();                // 按回车/发送按钮时发出，告诉外部可以发了

protected:
    void dragEnterEvent(QDragEnterEvent *event);//拖动文件进入时：判断能不能接住（图片和txt都收）
    void dropEvent(QDropEvent *event);         // 松开鼠标时：真正把文件内容插进来
    void keyPressEvent(QKeyEvent *e);          // 按回车发送；其它键交给基类

private:
    void insertImages(const QString &url);      // 插入一张图片消息
    void insertTextFile(const QString &url);    // 插入一个文本文件消息（做成文件卡片的样子）
    bool canInsertFromMimeData(const QMimeData *source) const;// 判断拖进来的东西我们收不收
    void insertFromMimeData(const QMimeData *source);        // 真正插入

private:
    bool isImage(QString url);//判断文件是否为图片（看后缀）
    void insertMsgList(QVector<MsgInfo> &list,QString flag, QString text, QPixmap pix);// 往消息列表里追加一条

    QStringList getUrl(QString text);//从文本里正则提取出所有文件路径/网址
    QPixmap getFileIconPixmap(const QString &url);//获取文件图标及大小信息，并转化成图片
    QString getFileSize(qint64 size);//获取文件大小（把字节数转成 "12.3 KB" 这种）

private slots:
    void textEditChanged();//内容变化：更新"发送按钮是否可点"的状态

private:
    QVector<MsgInfo> mMsgList;   // 输入框里现有的消息列表
    QVector<MsgInfo> mGetMsgList;// 取出消息时用的临时副本
};

#endif // MESSAGETEXTEDIT_H
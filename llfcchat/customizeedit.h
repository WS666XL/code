#ifndef CUSTOMIZEEDIT_H
#define CUSTOMIZEEDIT_H
#include <QLineEdit>
#include <QDebug>

/******************************************************************************
 *  文件：customizeedit.h
 *  作用：带长度限制的输入框（搜索框、用户名框在用）
 *  为什么要自己限制：QLineEdit 自带的setMaxLength 是按"字符个数"算的，
 *        一个汉字算 1个；这里按"UTF-8 字节数"算，避免中文被截成半个乱码
 *****************************************************************************/
class CustomizeEdit: public QLineEdit
{
    Q_OBJECT
public:
    CustomizeEdit(QWidget *parent = nullptr);
    void SetMaxLength(int maxLen);  // 设置最大长度（字节数）。传 0 表示不限制
protected:
    void focusOutEvent(QFocusEvent *event) override
    {
        // 执行失去焦点时的处理逻辑
        //qDebug() << "CustomizeEdit focusout";
        // 调用基类的focusOutEvent()方法，保证基类的行为得到执行
        QLineEdit::focusOutEvent(event);
        //发送失去焦点得信号
        emit sig_foucus_out();
    }
private:
    // 文本变化时自动调用：超长就从末尾截断
    void limitTextLength(QString text) {
        if(_max_len <= 0){
            return;   // 没设上限，不管
        }

        QByteArray byteArray = text.toUtf8();//把字符串 `text` 转成 UTF-8 编码的字节数组。

        if (byteArray.size() > _max_len) {
            byteArray = byteArray.left(_max_len);
            this->setText(QString::fromUtf8(byteArray));
        }
    }

    int _max_len;   // 最大字节数，0 表示不限制
signals:
    void sig_foucus_out();  // 失去焦点信号（注意：拼写就是 foucus，原来的笔误，勿改）
};

#endif // CUSTOMIZEEDIT_H
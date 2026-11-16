#ifndef LOADINGDLG_H
#define LOADINGDLG_H

#include <QDialog>

namespace Ui {
// 前向声明：由 uic 根据 loadingdlg.ui 自动生成
class LoadingDlg;
}

/******************************************************************************
 *  文件：loadingdlg.h
 *  作用："加载中"的全屏蒙层弹窗（中间放一个 loading.gif 转圈）
 *  样式：无边框 + 背景透明 + 铺满父窗口
 *****************************************************************************/
class LoadingDlg : public QDialog
{
    Q_OBJECT

public:
    explicit LoadingDlg(QWidget *parent = nullptr);
    ~LoadingDlg();

private:
    Ui::LoadingDlg *ui;  // 界面对象，里面就一个 loading_lb
};

#endif // LOADINGDLG_H

#ifndef FINDSUCCESSDLG_H
#define FINDSUCCESSDLG_H

/******************************************************************************
 *  文件：findsuccessdlg.h
 *  作用：搜索好友成功后弹出的"用户资料 + 添加好友"对话框
 *  说明：界面布局在 findsuccessdlg.ui 里，编译时自动生成 ui_findsuccessdlg.h
 *****************************************************************************/

#include"userdata.h"   // 提供 SearchInfo 结构（搜到的那个人的资料）
#include <QDialog>     // 继承自 QDialog，才能以弹窗形式显示
#include<memory>       // 使用 shared_ptr
namespace Ui {
// 前向声明：由 uic 根据 findsuccessdlg.ui 自动生成的界面类，这里先声明存在
class FindSuccessDlg;
}

// 搜索成功的对话框：左边显示对方头像和用户名，右边一个"添加"按钮
class FindSuccessDlg : public QDialog
{
    Q_OBJECT   // 类里用到了槽函数，必须写这个宏，否则 moc 不会生成槽表

public:
    explicit FindSuccessDlg(QWidget *parent = nullptr);
    ~FindSuccessDlg();  // 析构里 delete ui，释放 setupUi 出来的界面对象

    // 把搜到的用户资料交给对话框去显示（用户名显示到 name_lb 上）
    void SetSearchInfo(std::shared_ptr<SearchInfo> si);

private:
    Ui::FindSuccessDlg *ui;                 // 界面对象，用 ui->控件名 访问 ui 里的控件
    QWidget * _parent;                      // 记住父窗口是谁
    std::shared_ptr<SearchInfo> _si;        // 共享指针，保存搜到的用户资料；
                                             // 用 shared_ptr 是因为 SearchList 那边也还持有同一份数据

private slots:
    // 命名规则 on_控件名_信号名，Qt 会自动把它和 ui 里的 add_friend_btn 按钮的 clicked() 连上，
    // 不需要手写 connect
    void on_add_friend_btn_clicked();        // 点"添加"按钮：发添加好友请求（还没写，函数体里是 todo）

};
#endif // FINDSUCCESSDLG_H

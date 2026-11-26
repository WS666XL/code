#ifndef RESETDIALOG_H
#define RESETDIALOG_H

#include <QDialog>
#include "global.h"

namespace Ui {
class ResetDialog;
}

// 重置密码对话框：填用户名 / 邮箱 / 新密码 / 验证码，请求后端修改密码
class ResetDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ResetDialog(QWidget *parent = nullptr);
    ~ResetDialog();

private slots:
    void on_return_btn_clicked();   // "返回"按钮的槽：发出 switchLogin 让主窗口切回登录页

    void on_varify_btn_clicked();   // "获取"按钮的槽：请求后端把验证码发到邮箱

    void slot_reset_mod_finish(ReqId id, QString res, ErrorCodes err);   // 收到 HttpMgr 回包后处理
    void on_sure_btn_clicked();     // "确认"按钮的槽：逐项校验后发起重置密码请求

private:
    bool checkUserValid();          // 校验用户名：不能为空
    bool checkPassValid();          // 校验新密码：长度 6~15，且只含允许的字符
    void showTip(QString str,bool b_ok);   // 显示提示文字，b_ok 决定用正常样式还是错误样式
    bool checkEmailValid();         // 校验邮箱：必须符合邮箱格式
    bool checkVarifyValid();        // 校验验证码：不能为空
    void AddTipErr(TipErr te,QString tips);   // 记录一条错误提示并立即显示
    void DelTipErr(TipErr te);                // 解除某类错误提示（没有其它错误就清空提示栏）
    void initHandlers();            // 注册"请求编号 → 处理函数"的分发表
    Ui::ResetDialog *ui;            // 指向自动生成的界面对象，用 ui->控件名 访问控件
    QMap<TipErr, QString> _tip_errs;   // 当前待显示的错误提示集合（避免多条提示互相覆盖）
    QMap<ReqId, std::function<void(const QJsonObject&)>> _handlers;   // 分发表：按请求编号找到对应处理函数
signals:
    void switchLogin();   // 自定义信号：请求主窗口切回登录界面
};

#endif // RESETDIALOG_H

#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H

#include <QDialog>
#include"global.h"
namespace Ui {
class RegisterDialog;
}

// 注册对话框：负责邮箱校验、发验证码、接收后端回包并给出提示
class RegisterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RegisterDialog(QWidget *parent = nullptr);
    ~RegisterDialog();

private slots:
    void on_get_code_clicked();   // 命名规则：on_控件名_信号名，Qt 会自动连接到 ui 里的 get_code 按钮(自动连接)
    void on_sure_btn_clicked();   // "确认"按钮的槽：逐项校验输入后发起注册请求
    void on_return_btn_clicked(); // "返回"按钮的槽：发出 sigSwitchLogin 让主窗口切回登录页
    void on_concel_btn_clicked(); // "取消"按钮的槽：同样返回登录页

public slots:
     void slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err);   // 收到 HttpMgr 的回包后处理
signals:
    void sigSwitchLogin();        // 自定义信号：请求主窗口切回登录界面
private:
    void initHttpHandlers();      // 注册"请求编号 → 处理函数"的对应关系
    bool checkPassValid();        // 校验密码：长度 6~15，且只含允许的字符
    bool checkUserValid();        // 校验用户名：不能为空
    bool checkEmailValid();       // 校验邮箱：必须符合邮箱格式
    bool checkConfirmValid();     // 校验确认密码：格式合法且与密码一致
    bool checkVarifyValid();      // 校验验证码：不能为空
    void  ChangeTipPage();        // 切到"注册成功"提示页(page_2)，并启动 5 秒倒计时
    void AddTipErr(TipErr te, QString tips);   // 记录一条错误提示并立即显示
    void DelTipErr(TipErr te);                 // 解除某类错误提示（没有其它错误就清空提示栏）
    void showTip(QString str,bool b_ok );  // 显示提示文字，b_ok 决定用正常样式还是错误样式
    Ui::RegisterDialog *ui;       // 指向自动生成的界面对象，用 ui->控件名 访问控件
    // 分发表：根据请求编号(ReqId)找到对应的处理函数，参数是解析后的 JSON 对象
    // 好处是以后新增接口只需往这里加一条，不用写一堆 if/else
    QMap<ReqId, std::function<void(const QJsonObject&)>> _handlers;
    QMap<TipErr, QString> _tip_errs;   // 当前待显示的错误提示集合（避免多条提示互相覆盖）

    QTimer * _countdown_timer;    // 倒计时定时器：每秒刷新提示文字，归零后回登录页
    int _countdown;               // 剩余秒数
};

#endif // REGISTERDIALOG_H

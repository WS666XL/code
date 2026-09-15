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
    void slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err);   // 收到 HttpMgr 的回包后处理
private:
    void initHttpHandlers();      // 注册"请求编号 → 处理函数"的对应关系
    void showTip(QString str,bool b_ok );  // 显示提示文字，b_ok 决定用正常样式还是错误样式
    Ui::RegisterDialog *ui;
    // 分发表：根据请求编号(ReqId)找到对应的处理函数，参数是解析后的 JSON 对象
    // 好处是以后新增接口只需往这里加一条，不用写一堆 if/else
    QMap<ReqId, std::function<void(const QJsonObject&)>> _handlers;

};

#endif // REGISTERDIALOG_H

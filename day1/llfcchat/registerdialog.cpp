#include "registerdialog.h"
#include "ui_registerdialog.h"
#include"global.h"
#include"httpmgr.h"

RegisterDialog::RegisterDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RegisterDialog)
{
    ui->setupUi(this);
    // 两个输入框设置为密码模式(输入内容显示成黑点)
    ui->pass_edit->setEchoMode(QLineEdit::Password);
    ui->comfirm_edit->setEchoMode(QLineEdit::Password);

    ui->err_tip->setProperty("state","normal");//通过 setProperty() 修改控件自定义属性，用 QSS 属性选择器 #xxx[state="xx"] 切换样式时。
   // Qt 不会自动感知自定义属性变化，样式不会自动重绘，必须手动刷：
    repolish(ui->err_tip);

    // 订阅 HttpMgr 的注册模块回包信号：HTTP 请求完成后会走到 slot_reg_mod_finish
    connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_reg_mod_finish, this, &RegisterDialog::slot_reg_mod_finish);
    // 初始化"请求编号 → 处理函数"的分发表 _handlers。
    // 顺序很关键：必须在收到任何回包之前调用。否则 _handlers 是空表，
    // slot_reg_mod_finish 里调用空的 std::function 会抛 std::bad_function_call 直接崩溃。
    initHttpHandlers();
}

RegisterDialog::~RegisterDialog()
{
    delete ui;
}

// 点击"获取验证码"按钮时触发
void RegisterDialog::on_get_code_clicked()
{
    auto email = ui->email_edit->text().trimmed(); // 顺便去除首尾空格
    // 严谨版邮箱正则
    QRegularExpression regex(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
    bool isOk = regex.match(email).hasMatch();//匹配

    if (isOk)
    {
        //发送http请求获取验证码
        QJsonObject json_obj;                          // 请求体：把邮箱塞进 JSON 对象
        json_obj["email"] = email;

        // gate_url_prefix 是 main.cpp 从 config.ini 读出来的前缀(如 http://localhost:8080)，
        // 这里拼上具体接口名 → 最终地址 http://localhost:8080/get_varifycode
        // 后两个参数告诉 HttpMgr：这次是"取验证码"这个操作、由"注册模块"发起，
        // 这样回包时才能找到 _handlers 里对应的处理函数
        HttpMgr::GetInstance()->PostHttpReq(QUrl(gate_url_prefix+"/get_varifycode"),
                                            json_obj, ReqId::ID_GET_VARIFY_CODE,Modules::REGISTERMOD);
    }
    else
    {
        showTip(tr("邮箱地址格式不正确"),false);
    }
}

// HTTP 回包统一入口：先判断网络/解析是否正常，再按请求编号分发到具体处理函数
void RegisterDialog::slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err)
{
    if(err != ErrorCodes::SUCCESS){
        showTip(tr("网络请求错误"),false);
        return;
    }

    // 解析 JSON 字符串,res需转化为QByteArray
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    //json解析错误
    if(jsonDoc.isNull()){
        showTip(tr("json解析错误"),false);
        return;
    }

    //json解析错误
    if(!jsonDoc.isObject()){
        showTip(tr("json解析错误"),false);
        return;
    }

    //QJsonObject jsonObj = jsonDoc.object();
    // 按请求编号 id 到分发表里取处理函数并执行(具体逻辑见 initHttpHandlers)
    _handlers[id](jsonDoc.object());
    //调用对应的逻辑
    return;
}

// 登记"请求编号 → 处理函数"的对应关系（需要在构造函数里调用一次才能生效）
void RegisterDialog::initHttpHandlers()
{
    //注册获取验证码回包逻辑
    _handlers.insert(ReqId::ID_GET_VARIFY_CODE, [this](QJsonObject jsonObj){
        int error = jsonObj["error"].toInt();     // 后端返回的业务错误码
        if(error != ErrorCodes::SUCCESS){
            showTip(tr("参数错误"),false);
            return;
        }
        auto email = jsonObj["email"].toString(); // 后端回显的邮箱，用于确认发给谁了
        showTip(tr("验证码已发送到邮箱，注意查收"), true);
        qDebug()<< "email is " << email ;
    });
}

// 统一的提示显示：根据成功/失败切换 state 属性，再刷新样式
void RegisterDialog::showTip(QString str,bool b_ok)
{
    if(b_ok)
    {
        ui->err_tip->setProperty("state","normal");   // 成功：切到 normal，对应 qss 里 #err_tip[state='normal'] 的绿色
    }
    else
    {
        ui->err_tip->setProperty("state","err");      // 失败：切到 err，对应 qss 里的红色
    }
    ui->err_tip->setText(str);

    repolish(ui->err_tip);
}


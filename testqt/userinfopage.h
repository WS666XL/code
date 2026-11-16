#ifndef USERINFOPAGE_H
#define USERINFOPAGE_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

/******************************************************************************
 *  文件：userinfopage.h
 *  作用：侧边栏"设置"对应的用户信息页（基本信息展示 + 头像裁剪/本地保存）
 *  布局：纯代码搭建（不用 .ui），控件命名与老师 userinfopage.ui 保持一致：
 *        head_lb（头像）、up_btn（上传）、name_ed（只读）/ desc_ed（可编辑）
 *  本轮范围：不上传服务器（filetcpmgr 后续轮次再接），
 *        裁剪结果只写本地 AppData/<uid>/avatars/head.png 并发 sig_reset_head()
 *        让 chatdialog 刷新侧边栏头像。所以不放 submit_btn 占位。
 *****************************************************************************/
class UserInfoPage : public QWidget
{
    Q_OBJECT

public:
    explicit UserInfoPage(QWidget *parent = nullptr);

signals:
    // 头像已裁剪并保存成功（UserMgr 的 _icon 已同步成本地路径），
    // chatdialog 接它刷新侧边栏头像
    void sig_reset_head();
    // 描述保存成功：chatdialog 接它刷新当前聊天页里自己气泡的名字
    void sig_user_info_updated();

private slots:
    void slot_up_load();    // up_btn 点击：选图 → 裁剪 → 显示 → 本地保存
    void slot_save_info();  // 保存描述（1020/1022）

private:
    void InitUi();            // 纯代码搭建界面
    void LoadHeadFromLocal(); // 构造时按 UserMgr 里的 _icon 加载当前头像
    // 方形裁剪结果 → 圆形头像：圆形路径裁剪，圆外像素透明
    QPixmap MakeRoundPixmap(const QPixmap& src);

    QLabel* head_lb;       // 头像显示区（120x120 圆形）
    QPushButton* up_btn;   // "上传"按钮
    QPushButton* save_btn; // "保存"按钮（描述）
    QLineEdit* name_ed;    // 用户名（只读展示）
    QLineEdit* desc_ed;    // 描述（可编辑保存）
};

#endif // USERINFOPAGE_H

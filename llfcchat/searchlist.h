#ifndef SEARCHLIST_H
#define SEARCHLIST_H
#include<QListWidget>
#include<QWheelEvent>
#include<QEvent>
#include<QScrollBar>
#include<QDebug>
#include<QDialog>
#include<memory>
#include"userdata.h"
#include"loadingdlg.h"

/******************************************************************************
 *  文件：searchlist.h
 *  作用：搜索用户的结果列表（在搜索框里输入用户名后显示的那块）
 *  行为：和 ChatUserList 一样，滚动条悬停才显示；
 *        点"添加好友"提示条会弹出 FindSuccessDlg
 *****************************************************************************/
class SearchList: public QListWidget
{
    Q_OBJECT
public:
    SearchList(QWidget *parent = nullptr);
    void CloseFindDlg();          // 关闭搜索结果弹出的对话框
    void SetSearchEdit(QWidget* edit);  // 记住搜索框控件（还没实现）
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        // 检查事件是否是鼠标悬浮进入或离开
        if (watched == this->viewport()) {
            if (event->type() == QEvent::Enter) {
                // 鼠标悬浮，显示滚动条
                this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            } else if (event->type() == QEvent::Leave) {
                // 鼠标离开，隐藏滚动条
                this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            }
        }

        // 检查事件是否是鼠标滚轮事件
        if (watched == this->viewport() && event->type() == QEvent::Wheel) {
            QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);
            int numDegrees = wheelEvent->angleDelta().y() / 8;
            int numSteps = numDegrees / 15; // 计算滚动步数

            // 设置滚动幅度
            this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - numSteps);

            return true; // 停止事件传递
        }

        return QListWidget::eventFilter(watched, event);
    }
private:
    void waitPending(bool pending = true);  // 标记"正在等服务器回包"，防止重复发送（还没实现）
    bool _send_pending;                     // 是否正在等搜索结果
    void addTipItem();// 往列表里加"添加好友"提示条目
    std::shared_ptr<QDialog> _find_dlg;// 搜索结果详情弹窗，用 shared_ptr 管理
    QWidget* _search_edit;      // 搜索输入框（还没用上）
    LoadingDlg * _loadingDialog;// 加载动画对话框（还没用上）
private slots:
    void slot_item_clicked(QListWidgetItem *item);              // 点某一条：靠类型判断要不要弹窗
    void slot_user_search(std::shared_ptr<SearchInfo> si);// 收到搜索结果（还没实现）
signals:

};

#endif // SEARCHLIST_H

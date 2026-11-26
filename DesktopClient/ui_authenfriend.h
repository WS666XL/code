/********************************************************************************
** Form generated from reading UI file 'authenfriend.ui'
**
** Created by: Qt User Interface Compiler version 6.4.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_AUTHENFRIEND_H
#define UI_AUTHENFRIEND_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <clickedbtn.h>
#include <clickedoncelabel.h>
#include <customizeedit.h>

QT_BEGIN_NAMESPACE

class Ui_AuthenFriend
{
public:
    QVBoxLayout *verticalLayout;
    QScrollArea *scrollArea;
    QWidget *scrollcontent;
    QVBoxLayout *verticalLayout_12;
    QLabel *apply_lb;
    QWidget *apply_wid;
    QVBoxLayout *verticalLayout_13;
    QFrame *line;
    CustomizeEdit *name_ed;
    QLabel *label;
    CustomizeEdit *back_ed;
    QLabel *label_2;
    QWidget *lb_group_wid;
    QVBoxLayout *verticalLayout_14;
    QWidget *gridWidget;
    CustomizeEdit *lb_ed;
    QWidget *input_tip_wid;
    QVBoxLayout *verticalLayout_15;
    ClickedOnceLabel *tip_lb;
    QWidget *widget;
    QHBoxLayout *horizontalLayout_6;
    QWidget *lb_list;
    QWidget *more_lb_wid;
    QVBoxLayout *verticalLayout_16;
    QSpacerItem *verticalSpacer_7;
    ClickedOnceLabel *more_lb;
    QSpacerItem *verticalSpacer_8;
    QSpacerItem *verticalSpacer;
    QWidget *apply_sure_wid;
    QHBoxLayout *horizontalLayout_5;
    QSpacerItem *horizontalSpacer_7;
    ClickedBtn *sure_btn;
    QSpacerItem *horizontalSpacer_8;
    ClickedBtn *cancel_btn;
    QSpacerItem *horizontalSpacer_9;
    QSpacerItem *verticalSpacer_9;

    void setupUi(QDialog *AuthenFriend)
    {
        if (AuthenFriend->objectName().isEmpty())
            AuthenFriend->setObjectName("AuthenFriend");
        AuthenFriend->resize(422, 653);
        verticalLayout = new QVBoxLayout(AuthenFriend);
        verticalLayout->setObjectName("verticalLayout");
        scrollArea = new QScrollArea(AuthenFriend);
        scrollArea->setObjectName("scrollArea");
        scrollArea->setMinimumSize(QSize(0, 500));
        scrollArea->setWidgetResizable(true);
        scrollcontent = new QWidget();
        scrollcontent->setObjectName("scrollcontent");
        scrollcontent->setGeometry(QRect(0, 0, 408, 498));
        verticalLayout_12 = new QVBoxLayout(scrollcontent);
        verticalLayout_12->setSpacing(0);
        verticalLayout_12->setObjectName("verticalLayout_12");
        verticalLayout_12->setContentsMargins(0, -1, 0, -1);
        apply_lb = new QLabel(scrollcontent);
        apply_lb->setObjectName("apply_lb");
        apply_lb->setMinimumSize(QSize(0, 20));
        apply_lb->setMaximumSize(QSize(16777215, 20));

        verticalLayout_12->addWidget(apply_lb, 0, Qt::AlignmentFlag::AlignHCenter);

        apply_wid = new QWidget(scrollcontent);
        apply_wid->setObjectName("apply_wid");
        verticalLayout_13 = new QVBoxLayout(apply_wid);
        verticalLayout_13->setObjectName("verticalLayout_13");
        line = new QFrame(apply_wid);
        line->setObjectName("line");
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);

        verticalLayout_13->addWidget(line);

        name_ed = new CustomizeEdit(apply_wid);
        name_ed->setObjectName("name_ed");
        name_ed->setMinimumSize(QSize(0, 35));
        name_ed->setMaximumSize(QSize(16777215, 35));

        verticalLayout_13->addWidget(name_ed);

        label = new QLabel(apply_wid);
        label->setObjectName("label");
        label->setMinimumSize(QSize(0, 25));
        label->setMaximumSize(QSize(16777215, 25));

        verticalLayout_13->addWidget(label);

        back_ed = new CustomizeEdit(apply_wid);
        back_ed->setObjectName("back_ed");
        back_ed->setMinimumSize(QSize(0, 35));
        back_ed->setMaximumSize(QSize(16777215, 35));

        verticalLayout_13->addWidget(back_ed);

        label_2 = new QLabel(apply_wid);
        label_2->setObjectName("label_2");
        label_2->setMinimumSize(QSize(0, 25));
        label_2->setMaximumSize(QSize(16777215, 25));

        verticalLayout_13->addWidget(label_2);

        lb_group_wid = new QWidget(apply_wid);
        lb_group_wid->setObjectName("lb_group_wid");
        lb_group_wid->setMinimumSize(QSize(0, 50));
        verticalLayout_14 = new QVBoxLayout(lb_group_wid);
        verticalLayout_14->setSpacing(0);
        verticalLayout_14->setObjectName("verticalLayout_14");
        verticalLayout_14->setContentsMargins(0, 0, 0, 0);
        gridWidget = new QWidget(lb_group_wid);
        gridWidget->setObjectName("gridWidget");
        gridWidget->setMinimumSize(QSize(0, 40));
        gridWidget->setMaximumSize(QSize(16777215, 40));
        lb_ed = new CustomizeEdit(gridWidget);
        lb_ed->setObjectName("lb_ed");
        lb_ed->setGeometry(QRect(2, 2, 108, 35));
        lb_ed->setMaximumSize(QSize(16777215, 35));

        verticalLayout_14->addWidget(gridWidget);

        input_tip_wid = new QWidget(lb_group_wid);
        input_tip_wid->setObjectName("input_tip_wid");
        input_tip_wid->setMinimumSize(QSize(0, 35));
        input_tip_wid->setMaximumSize(QSize(16777215, 35));
        verticalLayout_15 = new QVBoxLayout(input_tip_wid);
        verticalLayout_15->setSpacing(0);
        verticalLayout_15->setObjectName("verticalLayout_15");
        verticalLayout_15->setContentsMargins(0, 0, 0, 0);
        tip_lb = new ClickedOnceLabel(input_tip_wid);
        tip_lb->setObjectName("tip_lb");

        verticalLayout_15->addWidget(tip_lb);


        verticalLayout_14->addWidget(input_tip_wid);

        widget = new QWidget(lb_group_wid);
        widget->setObjectName("widget");
        widget->setMinimumSize(QSize(0, 60));
        horizontalLayout_6 = new QHBoxLayout(widget);
        horizontalLayout_6->setSpacing(0);
        horizontalLayout_6->setObjectName("horizontalLayout_6");
        horizontalLayout_6->setContentsMargins(0, 0, 0, 0);
        lb_list = new QWidget(widget);
        lb_list->setObjectName("lb_list");
        lb_list->setMinimumSize(QSize(287, 50));
        lb_list->setMaximumSize(QSize(287, 16777215));

        horizontalLayout_6->addWidget(lb_list);

        more_lb_wid = new QWidget(widget);
        more_lb_wid->setObjectName("more_lb_wid");
        more_lb_wid->setMaximumSize(QSize(30, 16777215));
        verticalLayout_16 = new QVBoxLayout(more_lb_wid);
        verticalLayout_16->setObjectName("verticalLayout_16");
        verticalSpacer_7 = new QSpacerItem(20, 40, QSizePolicy::Expanding, QSizePolicy::Minimum);

        verticalLayout_16->addItem(verticalSpacer_7);

        more_lb = new ClickedOnceLabel(more_lb_wid);
        more_lb->setObjectName("more_lb");
        more_lb->setMinimumSize(QSize(25, 25));
        more_lb->setMaximumSize(QSize(25, 25));

        verticalLayout_16->addWidget(more_lb);

        verticalSpacer_8 = new QSpacerItem(20, 40, QSizePolicy::Expanding, QSizePolicy::Minimum);

        verticalLayout_16->addItem(verticalSpacer_8);


        horizontalLayout_6->addWidget(more_lb_wid);


        verticalLayout_14->addWidget(widget);


        verticalLayout_13->addWidget(lb_group_wid);


        verticalLayout_12->addWidget(apply_wid);

        scrollArea->setWidget(scrollcontent);

        verticalLayout->addWidget(scrollArea);

        verticalSpacer = new QSpacerItem(20, 10, QSizePolicy::Policy::Fixed, QSizePolicy::Minimum);

        verticalLayout->addItem(verticalSpacer);

        apply_sure_wid = new QWidget(AuthenFriend);
        apply_sure_wid->setObjectName("apply_sure_wid");
        apply_sure_wid->setMinimumSize(QSize(0, 50));
        horizontalLayout_5 = new QHBoxLayout(apply_sure_wid);
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        horizontalSpacer_7 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_7);

        sure_btn = new ClickedBtn(apply_sure_wid);
        sure_btn->setObjectName("sure_btn");
        sure_btn->setMinimumSize(QSize(100, 30));

        horizontalLayout_5->addWidget(sure_btn);

        horizontalSpacer_8 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_8);

        cancel_btn = new ClickedBtn(apply_sure_wid);
        cancel_btn->setObjectName("cancel_btn");
        cancel_btn->setMinimumSize(QSize(100, 30));

        horizontalLayout_5->addWidget(cancel_btn);

        horizontalSpacer_9 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_9);


        verticalLayout->addWidget(apply_sure_wid);

        verticalSpacer_9 = new QSpacerItem(20, 40, QSizePolicy::Expanding, QSizePolicy::Minimum);

        verticalLayout->addItem(verticalSpacer_9);


        retranslateUi(AuthenFriend);

        QMetaObject::connectSlotsByName(AuthenFriend);
    } // setupUi

    void retranslateUi(QDialog *AuthenFriend)
    {
        AuthenFriend->setWindowTitle(QCoreApplication::translate("AuthenFriend", "Dialog", nullptr));
        apply_lb->setText(QCoreApplication::translate("AuthenFriend", "\345\220\214\346\204\217\347\224\263\350\257\267\345\245\275\345\217\213", nullptr));
        label->setText(QCoreApplication::translate("AuthenFriend", "\345\244\207\346\263\250\345\220\215", nullptr));
        label_2->setText(QCoreApplication::translate("AuthenFriend", "\346\240\207\347\255\276", nullptr));
        tip_lb->setText(QString());
        more_lb->setText(QString());
        sure_btn->setText(QCoreApplication::translate("AuthenFriend", "\347\241\256\350\256\244", nullptr));
        cancel_btn->setText(QCoreApplication::translate("AuthenFriend", "\345\217\226\346\266\210", nullptr));
    } // retranslateUi

};

namespace Ui {
    class AuthenFriend: public Ui_AuthenFriend {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_AUTHENFRIEND_H

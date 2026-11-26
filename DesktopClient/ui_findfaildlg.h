/********************************************************************************
** Form generated from reading UI file 'findfaildlg.ui'
**
** Created by: Qt User Interface Compiler version 6.4.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_FINDFAILDLG_H
#define UI_FINDFAILDLG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <clickedbtn.h>

QT_BEGIN_NAMESPACE

class Ui_FindFailDlg
{
public:
    QVBoxLayout *verticalLayout;
    QSpacerItem *verticalSpacer;
    QLabel *fail_tip;
    QLabel *fail_tip2;
    QWidget *widget;
    ClickedBtn *fail_sure_btn;

    void setupUi(QDialog *FindFailDlg)
    {
        if (FindFailDlg->objectName().isEmpty())
            FindFailDlg->setObjectName("FindFailDlg");
        FindFailDlg->resize(250, 150);
        FindFailDlg->setMinimumSize(QSize(250, 150));
        FindFailDlg->setMaximumSize(QSize(250, 150));
        verticalLayout = new QVBoxLayout(FindFailDlg);
        verticalLayout->setObjectName("verticalLayout");
        verticalSpacer = new QSpacerItem(20, 20, QSizePolicy::Policy::Fixed, QSizePolicy::Minimum);

        verticalLayout->addItem(verticalSpacer);

        fail_tip = new QLabel(FindFailDlg);
        fail_tip->setObjectName("fail_tip");
        fail_tip->setMaximumSize(QSize(16777215, 25));

        verticalLayout->addWidget(fail_tip);

        fail_tip2 = new QLabel(FindFailDlg);
        fail_tip2->setObjectName("fail_tip2");
        fail_tip2->setMaximumSize(QSize(16777215, 25));

        verticalLayout->addWidget(fail_tip2);

        widget = new QWidget(FindFailDlg);
        widget->setObjectName("widget");
        fail_sure_btn = new ClickedBtn(widget);
        fail_sure_btn->setObjectName("fail_sure_btn");
        fail_sure_btn->setGeometry(QRect(60, 20, 100, 30));
        fail_sure_btn->setMinimumSize(QSize(100, 30));
        fail_sure_btn->setMaximumSize(QSize(100, 30));

        verticalLayout->addWidget(widget);


        retranslateUi(FindFailDlg);

        QMetaObject::connectSlotsByName(FindFailDlg);
    } // setupUi

    void retranslateUi(QDialog *FindFailDlg)
    {
        FindFailDlg->setWindowTitle(QCoreApplication::translate("FindFailDlg", "Dialog", nullptr));
        fail_tip->setText(QCoreApplication::translate("FindFailDlg", "\346\227\240\346\263\225\346\211\276\345\210\260\350\257\245\347\224\250\346\210\267", nullptr));
        fail_tip2->setText(QCoreApplication::translate("FindFailDlg", "\350\257\267\346\243\200\346\237\245\344\275\240\350\276\223\345\205\245\347\232\204\350\264\246\345\217\267\344\277\241\346\201\257\346\230\257\345\220\246\346\255\243\347\241\256", nullptr));
        fail_sure_btn->setText(QCoreApplication::translate("FindFailDlg", "\347\241\256\345\256\232", nullptr));
    } // retranslateUi

};

namespace Ui {
    class FindFailDlg: public Ui_FindFailDlg {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_FINDFAILDLG_H

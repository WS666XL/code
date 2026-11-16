/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.4.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QVBoxLayout *main_layout;
    QWidget *title_bar;
    QHBoxLayout *title_layout;
    QLabel *title_icon;
    QLabel *title_text;
    QSpacerItem *title_spacer;
    QToolButton *btn_min;
    QToolButton *btn_max;
    QToolButton *btn_close;
    QWidget *content_holder;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(300, 500);
        MainWindow->setMinimumSize(QSize(300, 500));
        MainWindow->setMaximumSize(QSize(300, 500));
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        main_layout = new QVBoxLayout(centralwidget);
        main_layout->setSpacing(0);
        main_layout->setObjectName("main_layout");
        main_layout->setContentsMargins(0, 0, 0, 0);
        title_bar = new QWidget(centralwidget);
        title_bar->setObjectName("title_bar");
        title_bar->setMinimumSize(QSize(0, 32));
        title_bar->setMaximumSize(QSize(16777215, 32));
        title_layout = new QHBoxLayout(title_bar);
        title_layout->setSpacing(6);
        title_layout->setObjectName("title_layout");
        title_layout->setContentsMargins(10, 0, 0, 0);
        title_icon = new QLabel(title_bar);
        title_icon->setObjectName("title_icon");
        title_icon->setMinimumSize(QSize(16, 16));
        title_icon->setMaximumSize(QSize(16, 16));
        title_icon->setScaledContents(true);

        title_layout->addWidget(title_icon);

        title_text = new QLabel(title_bar);
        title_text->setObjectName("title_text");

        title_layout->addWidget(title_text);

        title_spacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        title_layout->addItem(title_spacer);

        btn_min = new QToolButton(title_bar);
        btn_min->setObjectName("btn_min");

        title_layout->addWidget(btn_min);

        btn_max = new QToolButton(title_bar);
        btn_max->setObjectName("btn_max");

        title_layout->addWidget(btn_max);

        btn_close = new QToolButton(title_bar);
        btn_close->setObjectName("btn_close");

        title_layout->addWidget(btn_close);


        main_layout->addWidget(title_bar);

        content_holder = new QWidget(centralwidget);
        content_holder->setObjectName("content_holder");

        main_layout->addWidget(content_holder);

        MainWindow->setCentralWidget(centralwidget);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "xulei666", nullptr));
        title_text->setText(QCoreApplication::translate("MainWindow", "xulei666", nullptr));
        btn_min->setText(QCoreApplication::translate("MainWindow", "\342\224\200", nullptr));
        btn_max->setText(QCoreApplication::translate("MainWindow", "\342\226\241", nullptr));
        btn_close->setText(QCoreApplication::translate("MainWindow", "\342\234\225", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H

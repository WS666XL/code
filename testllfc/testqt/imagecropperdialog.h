#ifndef IMAGECROPPER_H
#define IMAGECROPPER_H

#include <QWidget>
#include <QDialog>
#include <QPainter>
#include <QLabel>
#include <QPixmap>
#include <QString>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>

#include "imagecropperlabel.h"

/*******************************************************
 *  Loacl private class, which do image-cropping
 *  Used in class ImageCropper
 *  真正干活的裁剪对话框：中间是 ImageCropperLabel（拖框选区），
 *  下面是 OK/Cancel 两个按钮。
 *  outputImage 用引用传出：点 OK 时把裁剪结果写回调用者的 QPixmap，
 *  点 Cancel 写回空图，调用方用 isNull() 区分。
*******************************************************/
class ImageCropperDialogPrivate : public QDialog {
    Q_OBJECT
public:
    ImageCropperDialogPrivate(const QPixmap& imageIn, QPixmap& outputImage,
                              int windowWidth, int windowHeight,
                              CropperShape shape, QSize cropperSize = QSize()) :
        QDialog(nullptr),  outputImage(outputImage)
    {
        this->setAttribute(Qt::WA_DeleteOnClose, true);   // 关闭即析构，防止泄露
        this->setWindowTitle("Image Cropper");
        this->setMouseTracking(true);   // 不按键也跟踪鼠标，裁剪框悬停变光标要用
        this->setModal(true);

        imageLabel = new ImageCropperLabel(windowWidth, windowHeight, this);
        imageLabel->setCropper(shape, cropperSize);
        imageLabel->setOutputShape(OutputShape::RECT);   // 输出方图（圆形观感由外面自己遮罩）
        imageLabel->setOriginalImage(imageIn);
        imageLabel->enableOpacity(true);   // 选区外画半透明遮罩

        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnOk = new QPushButton("OK", this);
        btnCancel = new QPushButton("Cancel", this);
        btnLayout->addStretch();
        btnLayout->addWidget(btnOk);
        btnLayout->addWidget(btnCancel);

        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(imageLabel);
        mainLayout->addLayout(btnLayout);

        connect(btnOk, &QPushButton::clicked, this, [this](){
            this->outputImage = this->imageLabel->getCroppedImage();
            this->close();
        });
        connect(btnCancel, &QPushButton::clicked, this, [this](){
            this->outputImage = QPixmap();   // 空图 = 用户取消
            this->close();
        });
    }

private:
    ImageCropperLabel* imageLabel;
    QPushButton* btnOk;
    QPushButton* btnCancel;
    QPixmap& outputImage;
};


/*******************************************************************
 *  class ImageCropperDialog
 *      create a instane of class ImageCropperDialogPrivate
 *      and get cropped image from the instance(after closing)
 *  对外只需这一个静态入口：给图片路径 + 窗口尺寸 + 裁剪框形状，
 *  exec() 模态阻塞到用户关框，返回裁剪结果（取消返回空图）
********************************************************************/
class ImageCropperDialog : QObject {
public:
    static QPixmap getCroppedImage(const QString& filename,int windowWidth, int windowHeight,
                                   CropperShape cropperShape, QSize crooperSize = QSize())
    {
        QPixmap inputImage;
        QPixmap outputImage;

        if (!inputImage.load(filename)) {
            QMessageBox::critical(nullptr, "Error", "Load image failed!", QMessageBox::Ok);
            return outputImage;
        }

        ImageCropperDialogPrivate* imageCropperDo =
            new ImageCropperDialogPrivate(inputImage, outputImage,
                                          windowWidth, windowHeight,
                                          cropperShape, crooperSize);
        imageCropperDo->exec();
        // WA_DeleteOnClose：exec() 返回时对话框已析构，但 outputImage 是
        // 本函数栈上的 QPixmap，引用在析构前已写好，这里按值拷贝返回
        return outputImage;
    }
};



#endif // IMAGECROPPER_H

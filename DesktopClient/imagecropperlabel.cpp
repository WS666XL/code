#include "imagecropperlabel.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDebug>
#include <QBitmap>

ImageCropperLabel::ImageCropperLabel(int width, int height, QWidget* parent) :
         QLabel(parent)
{
    this->setFixedSize(width, height);
    this->setAlignment(Qt::AlignCenter);
    // 不按鼠标键也要收 mouseMoveEvent：光标随悬停位置变化（八方向拉伸提示）全靠它
    this->setMouseTracking(true);

    // 裁剪框边框：白色虚线（3-3 相间）
    borderPen.setWidth(1);
    borderPen.setColor(Qt::white);
    borderPen.setDashPattern(QVector<qreal>() << 3 << 3 << 3 << 3);
}

// 等比缩放图片铺进 label，并记下缩放比例 scaledRate：
// 后面裁剪还原、FIXED 尺寸换算都要除以/乘以它，
// 因为鼠标事件给的都是 label 上的"显示坐标"，而真正要裁的是"原图坐标"
void ImageCropperLabel::setOriginalImage(const QPixmap &pixmap) {
    originalImage = pixmap;

    int imgWidth = pixmap.width();
    int imgHeight = pixmap.height();
    int labelWidth = this->width();
    int labelHeight = this->height();
    int imgWidthInLabel;
    int imgHeightInLabel;

    // 等比缩放：比较 imgW/imgH 与 labelW/labelH 谁更"窄"，
    // 以更受限的边为基准缩放，另一边按比例算出，再在 label 里居中
    if (imgWidth * labelHeight < imgHeight * labelWidth) {
        scaledRate = labelHeight / double(imgHeight);
        imgHeightInLabel = labelHeight;
        imgWidthInLabel = int(scaledRate * imgWidth);
        imageRect.setRect((labelWidth - imgWidthInLabel) / 2, 0,
                          imgWidthInLabel, imgHeightInLabel);
    }
    else {
        scaledRate = labelWidth / double(imgWidth);
        imgWidthInLabel = labelWidth;
        imgHeightInLabel = int(scaledRate * imgHeight);
        imageRect.setRect(0, (labelHeight - imgHeightInLabel) / 2,
                          imgWidthInLabel, imgHeightInLabel);
    }

    tempImage = originalImage.scaled(imgWidthInLabel, imgHeightInLabel,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
    this->setPixmap(tempImage);

    if (cropperShape >= CropperShape::FIXED_RECT) {
        // FIXED 形状：cropperRect_ 存的是原图坐标下的真实尺寸，
        // 显示时乘 scaledRate 变成 label 上的尺寸
        cropperRect.setWidth(int(cropperRect_.width() * scaledRate));
        cropperRect.setHeight(int(cropperRect_.height() * scaledRate));
    }
    resetCropperPos();
}


/*****************************************
 * set cropper's shape (and size)
*****************************************/
void ImageCropperLabel::setRectCropper() {
    cropperShape = CropperShape::RECT;
    resetCropperPos();
}

void ImageCropperLabel::setSquareCropper() {
    cropperShape = CropperShape::SQUARE;
    resetCropperPos();
}

void ImageCropperLabel::setEllipseCropper() {
    cropperShape = CropperShape::ELLIPSE;
    resetCropperPos();
}

void ImageCropperLabel::setCircleCropper() {
    cropperShape = CropperShape::CIRCLE;
    resetCropperPos();
}

void ImageCropperLabel::setFixedRectCropper(QSize size) {
    cropperShape = CropperShape::FIXED_RECT;
    cropperRect_.setSize(size);
    resetCropperPos();
}

void ImageCropperLabel::setFixedEllipseCropper(QSize size) {
    cropperShape = CropperShape::FIXED_ELLIPSE;
    cropperRect_.setSize(size);
    resetCropperPos();
}

// not recommended
void ImageCropperLabel::setCropper(CropperShape shape, QSize size) {
    cropperShape = shape;
    cropperRect_.setSize(size);
    resetCropperPos();
}

/*****************************************************************************
     * Set cropper's fixed size
    *****************************************************************************/
void ImageCropperLabel::setCropperFixedSize(int fixedWidth, int fixedHeight) {
    cropperRect_.setSize(QSize(fixedWidth, fixedHeight));
    resetCropperPos();
}

void ImageCropperLabel::setCropperFixedWidth(int fixedWidth) {
    cropperRect_.setWidth(fixedWidth);
    resetCropperPos();
}

void ImageCropperLabel::setCropperFixedHeight(int fixedHeight) {
    cropperRect_.setHeight(fixedHeight);
    resetCropperPos();
}

/**********************************************
 * Move cropper to the center of the image
 * And resize to default
**********************************************/
void ImageCropperLabel::resetCropperPos() {
    int labelWidth = this->width();
    int labelHeight = this->height();

    if (cropperShape == CropperShape::FIXED_RECT || cropperShape == CropperShape::FIXED_ELLIPSE) {
        cropperRect.setWidth(int(cropperRect_.width() * scaledRate));
        cropperRect.setHeight(int(cropperRect_.height() * scaledRate));
    }

    switch (cropperShape) {
        case CropperShape::UNDEFINED:
            break;
        case CropperShape::FIXED_RECT:
        case CropperShape::FIXED_ELLIPSE: {
            // 固定尺寸：直接在 label 中心摆好
            cropperRect.setRect((labelWidth - cropperRect.width()) / 2,
                             (labelHeight - cropperRect.height()) / 2,
                             cropperRect.width(), cropperRect.height());
            break;
        }
        case CropperShape::RECT:
        case CropperShape::SQUARE:
        case CropperShape::ELLIPSE:
        case CropperShape::CIRCLE: {
            // 默认取显示图短边的 3/4 作正方形选区，居中放置
            int imgWidth = tempImage.width();
            int imgHeight = tempImage.height();
            int edge = int((imgWidth > imgHeight ? imgHeight : imgWidth) * 3 / 4.0);
            cropperRect.setRect((labelWidth - edge) / 2, (labelHeight - edge) / 2, edge, edge);
            break;
        }
    }
}

QPixmap ImageCropperLabel::getCroppedImage() {
    return getCroppedImage(this->outputShape);
}

QPixmap ImageCropperLabel::getCroppedImage(OutputShape shape) {
    // 裁剪框记录的是 label 上的显示坐标，裁原图必须除以 scaledRate 还原成原图坐标：
    // 先算选区左上角相对图片显示区左上角的偏移（同样是显示坐标），再 / scaledRate
    int startX = int((cropperRect.left() - imageRect.left()) / scaledRate);
    int startY = int((cropperRect.top() - imageRect.top()) / scaledRate);
    int croppedWidth = int(cropperRect.width() / scaledRate);
    int croppedHeight = int(cropperRect.height() / scaledRate);

    QPixmap resultImage(croppedWidth, croppedHeight);
    resultImage = originalImage.copy(startX, startY, croppedWidth, croppedHeight);

    // Set ellipse mask (cut to ellipse shape)
    if (shape == OutputShape::ELLIPSE) {
        // QBitmap 是 1 位黑白位图，做 QPixmap 的掩码（mask）用：
        // 掩码某像素为 1（白）→ 原图对应像素显示；为 0（黑）→ 变透明遮掉。
        // 所以先整块刷白（全显示），再用黑刷子画圆角矩形（圈内变 0 被遮掉，
        // 得到的效果是"圆角矩形外保留"——本项目输出固定用 RECT，不走这个分支，
        // 这里保持上游实现原样，仅替换 Qt6 已删除的 drawRoundRect API）
        QSize size(croppedWidth, croppedHeight);
        QBitmap mask(size);
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.fillRect(0, 0, size.width(), size.height(), Qt::white);
        painter.setBrush(QColor(0, 0, 0));
        // Qt6 删除了 drawRoundRect(int,int,int,int,int,int)，
        // 等价写法是 drawRoundedRect + Qt::RelativeSize：
        // 99/99 表示圆角半径取宽/高一半的 99%，即最圆的胶囊形状，与 Qt5 语义一致
        painter.drawRoundedRect(QRect(0, 0, size.width(), size.height()), 99, 99, Qt::RelativeSize);
        resultImage.setMask(mask);
    }

    return resultImage;
}


void ImageCropperLabel::paintEvent(QPaintEvent *event) {
    // Draw original image
    QLabel::paintEvent(event);

    // Draw cropper and set some effects
    switch (cropperShape) {
        case CropperShape::UNDEFINED:
            break;
        case CropperShape::FIXED_RECT:
            drawRectOpacity();
            break;
        case CropperShape::FIXED_ELLIPSE:
            drawEllipseOpacity();
            break;
        case CropperShape::RECT:
            drawRectOpacity();
            drawSquareEdge(!ONLY_FOUR_CORNERS);
            break;
        case CropperShape::SQUARE:
            drawRectOpacity();
            drawSquareEdge(ONLY_FOUR_CORNERS);
            break;
        case CropperShape::ELLIPSE:
            drawEllipseOpacity();
            drawSquareEdge(!ONLY_FOUR_CORNERS);
            break;
        case CropperShape::CIRCLE:
            drawEllipseOpacity();
            drawSquareEdge(ONLY_FOUR_CORNERS);
            break;
    }

    // Draw cropper rect
    if (isShowRectBorder) {
        QPainter painter(this);
        painter.setPen(borderPen);
        painter.drawRect(cropperRect);
    }
}

// 画八方向拖拽小方块：四角必画，onlyFourCorners=false 时四边中点也画
// （RECT/ELLIPSE 可单独拉伸某一边，SQUARE/CIRCLE 只能拖四角保持等比）
void ImageCropperLabel::drawSquareEdge(bool onlyFourCorners) {
    if (!isShowDragSquare)
        return;

    // Four corners
    drawFillRect(cropperRect.topLeft(), dragSquareEdge, dragSquareColor);
    drawFillRect(cropperRect.topRight(), dragSquareEdge, dragSquareColor);
    drawFillRect(cropperRect.bottomLeft(), dragSquareEdge, dragSquareColor);
    drawFillRect(cropperRect.bottomRight(), dragSquareEdge, dragSquareColor);

    // Four edges
    if (!onlyFourCorners) {
        int centralX = cropperRect.left() + cropperRect.width() / 2;
        int centralY = cropperRect.top() + cropperRect.height() / 2;
        drawFillRect(QPoint(cropperRect.left(), centralY), dragSquareEdge, dragSquareColor);
        drawFillRect(QPoint(centralX, cropperRect.top()), dragSquareEdge, dragSquareColor);
        drawFillRect(QPoint(cropperRect.right(), centralY), dragSquareEdge, dragSquareColor);
        drawFillRect(QPoint(centralX, cropperRect.bottom()), dragSquareEdge, dragSquareColor);
    }
}

void ImageCropperLabel::drawFillRect(QPoint centralPoint, int edge, QColor color) {
    QRect rect(centralPoint.x() - edge / 2, centralPoint.y() - edge / 2, edge, edge);
    QPainter painter(this);
    painter.fillRect(rect, color);
}

// Opacity effect
void ImageCropperLabel::drawOpacity(const QPainterPath& path) {
    QPainter painterOpac(this);
    painterOpac.setOpacity(opacity);
    painterOpac.fillPath(path, QBrush(Qt::black));
}

// 选区外遮罩 = 整图区域路径 减去 选区路径（QPainterPath::subtracted）
void ImageCropperLabel::drawRectOpacity() {
    if (isShowOpacityEffect) {
        QPainterPath p1, p2, p;
        p1.addRect(imageRect);
        p2.addRect(cropperRect);
        p = p1.subtracted(p2);
        drawOpacity(p);
    }
}

void ImageCropperLabel::drawEllipseOpacity() {
    if (isShowOpacityEffect) {
        QPainterPath p1, p2, p;
        p1.addRect(imageRect);
        p2.addEllipse(cropperRect);
        p = p1.subtracted(p2);
        drawOpacity(p);
    }
}

bool ImageCropperLabel::isPosNearDragSquare(const QPoint& pt1, const QPoint& pt2) {
    // Qt6/跨平台用 qAbs 代替 abs（int 版本，语义同 abs）
    return qAbs(pt1.x() - pt2.x()) * 2 <= dragSquareEdge
           && qAbs(pt1.y() - pt2.y()) * 2 <= dragSquareEdge;
}

// 判断鼠标落点属于裁剪框的哪个部位（哪个拖拽块/框内/框外），
// 决定之后拖动时怎么改 cropperRect。顺序很重要：先查八个拖拽块，再查框内
int ImageCropperLabel::getPosInCropperRect(const QPoint &pt) {
    if (isPosNearDragSquare(pt, QPoint(cropperRect.right(), cropperRect.center().y())))
        return RECT_RIGHT;
    if (isPosNearDragSquare(pt, cropperRect.bottomRight()))
        return RECT_BOTTOM_RIGHT;
    if (isPosNearDragSquare(pt, QPoint(cropperRect.center().x(), cropperRect.bottom())))
        return RECT_BOTTOM;
    if (isPosNearDragSquare(pt, cropperRect.bottomLeft()))
        return RECT_BOTTOM_LEFT;
    if (isPosNearDragSquare(pt, QPoint(cropperRect.left(), cropperRect.center().y())))
        return RECT_LEFT;
    if (isPosNearDragSquare(pt, cropperRect.topLeft()))
        return RECT_TOP_LEFT;
    if (isPosNearDragSquare(pt, QPoint(cropperRect.center().x(), cropperRect.top())))
        return RECT_TOP;
    if (isPosNearDragSquare(pt, cropperRect.topRight()))
        return RECT_TOP_RIGHT;
    if (cropperRect.contains(pt, true))
        return RECT_INSIDE;
    return RECT_OUTSIZD;
}

/*************************************************
 *
 *  Change mouse cursor type
 *      Arrow, SizeHor, SizeVer, etc...
 *
*************************************************/

void ImageCropperLabel::changeCursor() {
    switch (cursorPosInCropperRect) {
        case RECT_OUTSIZD:
            setCursor(Qt::ArrowCursor);
            break;
        case RECT_BOTTOM_RIGHT: {
            switch (cropperShape) {
            case CropperShape::SQUARE:
            case CropperShape::CIRCLE:
            case CropperShape::RECT:
            case CropperShape::ELLIPSE:
                setCursor(Qt::SizeFDiagCursor);
                break;
            default:
                break;
            }
            break;
        }
        case RECT_RIGHT: {
            switch (cropperShape) {
            case CropperShape::RECT:
            case CropperShape::ELLIPSE:
                setCursor(Qt::SizeHorCursor);
                break;
            default:
                break;
            }
            break;
        }
        case RECT_BOTTOM: {
            switch (cropperShape) {
            case CropperShape::RECT:
            case CropperShape::ELLIPSE:
                setCursor(Qt::SizeVerCursor);
                break;
            default:
                break;
            }
            break;
        }
        case RECT_BOTTOM_LEFT: {
            switch (cropperShape) {
            case CropperShape::RECT:
            case CropperShape::ELLIPSE:
            case CropperShape::SQUARE:
            case CropperShape::CIRCLE:
                setCursor(Qt::SizeBDiagCursor);
                break;
            default:
                break;
            }
            break;
        }
        case RECT_LEFT: {
            switch (cropperShape) {
            case CropperShape::RECT:
            case CropperShape::ELLIPSE:
                setCursor(Qt::SizeHorCursor);
                break;
            default:
                break;
            }
            break;
        }
        case RECT_TOP_LEFT: {
            switch (cropperShape) {
            case CropperShape::RECT:
            case CropperShape::ELLIPSE:
            case CropperShape::SQUARE:
            case CropperShape::CIRCLE:
                setCursor(Qt::SizeFDiagCursor);
                break;
            default:
                break;
            }
            break;
        }
        case RECT_TOP: {
            switch (cropperShape) {
            case CropperShape::RECT:
            case CropperShape::ELLIPSE:
                setCursor(Qt::SizeVerCursor);
                break;
            default:
                break;
            }
            break;
        }
        case RECT_TOP_RIGHT: {
            switch (cropperShape) {
            case CropperShape::SQUARE:
            case CropperShape::CIRCLE:
            case CropperShape::RECT:
            case CropperShape::ELLIPSE:
                setCursor(Qt::SizeBDiagCursor);
                break;
            default:
                break;
            }
            break;
        }
        case RECT_INSIDE: {
            setCursor(Qt::SizeAllCursor);
            break;
        }
    }
}

/*****************************************************
 *
 *  Mouse Events
 *
*****************************************************/

// Qt6：QMouseEvent::pos() 被移除，坐标统一走 position().toPoint()
void ImageCropperLabel::mousePressEvent(QMouseEvent *e) {
    currPos = lastPos = e->position().toPoint();
    isLButtonPressed = true;
}

void ImageCropperLabel::mouseMoveEvent(QMouseEvent *e) {
    currPos = e->position().toPoint();
    if (!isCursorPosCalculated) {
        cursorPosInCropperRect = getPosInCropperRect(currPos);
        changeCursor();
    }

    if (!isLButtonPressed)
        return;
    // 拖出图片显示区就不响应，保证选区不会被拖出图片
    if (!imageRect.contains(currPos))
        return;

    isCursorPosCalculated = true;

    int xOffset = currPos.x() - lastPos.x();
    int yOffset = currPos.y() - lastPos.y();
    lastPos = currPos;

    int disX = 0;
    int disY = 0;

    // Move cropper
    switch (cursorPosInCropperRect) {
        case RECT_OUTSIZD:
            break;
        case RECT_BOTTOM_RIGHT: {
            disX = currPos.x() - cropperRect.left();
            disY = currPos.y() - cropperRect.top();
            switch (cropperShape) {
                case CropperShape::UNDEFINED:
                case CropperShape::FIXED_RECT:
                case CropperShape::FIXED_ELLIPSE:
                    break;
                case CropperShape::SQUARE:
                case CropperShape::CIRCLE:
                    // 正方形/圆形：宽高必须同步变，取 disX/disY 中较小的方向生效
                    setCursor(Qt::SizeFDiagCursor);
                    if (disX >= cropperMinimumWidth && disY >= cropperMinimumHeight) {
                        if (disX > disY && cropperRect.top() + disX <= imageRect.bottom()) {
                            cropperRect.setRight(currPos.x());
                            cropperRect.setBottom(cropperRect.top() + disX);
                            emit croppedImageChanged();
                        }
                        else if (disX <= disY && cropperRect.left() + disY <= imageRect.right()) {
                            cropperRect.setBottom(currPos.y());
                            cropperRect.setRight(cropperRect.left() + disY);
                            emit croppedImageChanged();
                        }
                    }
                    break;
                case CropperShape::RECT:
                case CropperShape::ELLIPSE:
                    // 矩形/椭圆：宽高独立变化
                    setCursor(Qt::SizeFDiagCursor);
                    if (disX >= cropperMinimumWidth) {
                        cropperRect.setRight(currPos.x());
                        emit croppedImageChanged();
                    }
                    if (disY >= cropperMinimumHeight) {
                        cropperRect.setBottom(currPos.y());
                        emit croppedImageChanged();
                    }
                    break;
            }
            break;
        }
        case RECT_RIGHT: {
            disX = currPos.x() - cropperRect.left();
            switch (cropperShape) {
                case CropperShape::UNDEFINED:
                case CropperShape::FIXED_RECT:
                case CropperShape::FIXED_ELLIPSE:
                case CropperShape::SQUARE:
                case CropperShape::CIRCLE:
                    break;
                case CropperShape::RECT:
                case CropperShape::ELLIPSE:
                    if (disX >= cropperMinimumWidth) {
                        cropperRect.setRight(currPos.x());
                        emit croppedImageChanged();
                    }
                    break;
            }
            break;
        }
        case RECT_BOTTOM: {
            disY = currPos.y() - cropperRect.top();
            switch (cropperShape) {
                case CropperShape::UNDEFINED:
                case CropperShape::FIXED_RECT:
                case CropperShape::FIXED_ELLIPSE:
                case CropperShape::SQUARE:
                case CropperShape::CIRCLE:
                    break;
                case CropperShape::RECT:
                case CropperShape::ELLIPSE:
                    if (disY >= cropperMinimumHeight) {
                        cropperRect.setBottom(cropperRect.bottom() + yOffset);
                        emit croppedImageChanged();
                    }
                    break;
            }
            break;
        }
        case RECT_BOTTOM_LEFT: {
            disX = cropperRect.right() - currPos.x();
            disY = currPos.y() - cropperRect.top();
            switch (cropperShape) {
                case CropperShape::UNDEFINED:
                    break;
                case CropperShape::FIXED_RECT:
                case CropperShape::FIXED_ELLIPSE:
                case CropperShape::RECT:
                case CropperShape::ELLIPSE:
                    if (disX >= cropperMinimumWidth) {
                        cropperRect.setLeft(currPos.x());
                        emit croppedImageChanged();
                    }
                    if (disY >= cropperMinimumHeight) {
                        cropperRect.setBottom(currPos.y());
                        emit croppedImageChanged();
                    }
                    break;
                case CropperShape::SQUARE:
                case CropperShape::CIRCLE:
                    if (disX >= cropperMinimumWidth && disY >= cropperMinimumHeight) {
                        if (disX > disY && cropperRect.top() + disX <= imageRect.bottom()) {
                            cropperRect.setLeft(currPos.x());
                            cropperRect.setBottom(cropperRect.top() + disX);
                            emit croppedImageChanged();
                        }
                        else if (disX <= disY && cropperRect.right() - disY >= imageRect.left()) {
                            cropperRect.setBottom(currPos.y());
                            cropperRect.setLeft(cropperRect.right() - disY);
                            emit croppedImageChanged();
                        }
                    }
                    break;
            }
            break;
        }
        case RECT_LEFT: {
            disX = cropperRect.right() - currPos.x();
            switch (cropperShape) {
                case CropperShape::UNDEFINED:
                case CropperShape::FIXED_RECT:
                case CropperShape::FIXED_ELLIPSE:
                case CropperShape::SQUARE:
                case CropperShape::CIRCLE:
                    break;
                case CropperShape::RECT:
                case CropperShape::ELLIPSE:
                    if (disX >= cropperMinimumHeight) {
                        cropperRect.setLeft(cropperRect.left() + xOffset);
                        emit croppedImageChanged();
                    }
                    break;
            }
            break;
        }
        case RECT_TOP_LEFT: {
            disX = cropperRect.right() - currPos.x();
            disY = cropperRect.bottom() - currPos.y();
            switch (cropperShape) {
                case CropperShape::UNDEFINED:
                case CropperShape::FIXED_RECT:
                case CropperShape::FIXED_ELLIPSE:
                    break;
                case CropperShape::RECT:
                case CropperShape::ELLIPSE:
                    if (disX >= cropperMinimumWidth) {
                        cropperRect.setLeft(currPos.x());
                        emit croppedImageChanged();
                    }
                    if (disY >= cropperMinimumHeight) {
                        cropperRect.setTop(currPos.y());
                        emit croppedImageChanged();
                    }
                    break;
                case CropperShape::SQUARE:
                case CropperShape::CIRCLE:
                    if (disX >= cropperMinimumWidth && disY >= cropperMinimumHeight) {
                        if (disX > disY && cropperRect.bottom() - disX >= imageRect.top()) {
                            cropperRect.setLeft(currPos.x());
                            cropperRect.setTop(cropperRect.bottom() - disX);
                            emit croppedImageChanged();
                        }
                        else if (disX <= disY && cropperRect.right() - disY >= imageRect.left()) {
                            cropperRect.setTop(currPos.y());
                            cropperRect.setLeft(cropperRect.right() - disY);
                            emit croppedImageChanged();
                        }
                    }
                    break;
            }
            break;
        }
        case RECT_TOP: {
            disY = cropperRect.bottom() - currPos.y();
            switch (cropperShape) {
                case CropperShape::UNDEFINED:
                case CropperShape::FIXED_RECT:
                case CropperShape::FIXED_ELLIPSE:
                case CropperShape::SQUARE:
                case CropperShape::CIRCLE:
                    break;
                case CropperShape::RECT:
                case CropperShape::ELLIPSE:
                    if (disY >= cropperMinimumHeight) {
                        cropperRect.setTop(cropperRect.top() + yOffset);
                        emit croppedImageChanged();
                    }
                    break;
            }
            break;
        }
        case RECT_TOP_RIGHT: {
            disX = currPos.x() - cropperRect.left();
            disY = cropperRect.bottom() - currPos.y();
            switch (cropperShape) {
                case CropperShape::UNDEFINED:
                case CropperShape::FIXED_RECT:
                case CropperShape::FIXED_ELLIPSE:
                    break;
                case CropperShape::RECT:
                case CropperShape::ELLIPSE:
                    if (disX >= cropperMinimumWidth) {
                        cropperRect.setRight(currPos.x());
                        emit croppedImageChanged();
                    }
                    if (disY >= cropperMinimumHeight) {
                        cropperRect.setTop(currPos.y());
                        emit croppedImageChanged();
                    }
                    break;
                case CropperShape::SQUARE:
                case CropperShape::CIRCLE:
                    if (disX >= cropperMinimumWidth && disY >= cropperMinimumHeight) {
                        if (disX < disY && cropperRect.left() + disY <= imageRect.right()) {
                            cropperRect.setTop(currPos.y());
                            cropperRect.setRight(cropperRect.left() + disY);
                            emit croppedImageChanged();
                        }
                        else if (disX >= disY && cropperRect.bottom() - disX >= imageRect.top()) {
                            cropperRect.setRight(currPos.x());
                            cropperRect.setTop(cropperRect.bottom() - disX);
                            emit croppedImageChanged();
                        }
                    }
                    break;
            }
            break;
        }
        case RECT_INSIDE: {
            // 拖动整个选区：先按方向做边界钳制，
            // 保证 cropperRect 始终完整落在 imageRect 里
            if (xOffset > 0) {
                if (cropperRect.right() + xOffset > imageRect.right())
                    xOffset = 0;
            }
            else if (xOffset < 0) {
                if (cropperRect.left() + xOffset < imageRect.left())
                    xOffset = 0;
            }
            if (yOffset > 0) {
                if (cropperRect.bottom() + yOffset > imageRect.bottom())
                    yOffset = 0;
            }
            else if (yOffset < 0) {
                if (cropperRect.top() + yOffset < imageRect.top())
                    yOffset = 0;
            }
            cropperRect.moveTo(cropperRect.left() + xOffset, cropperRect.top() + yOffset);
            emit croppedImageChanged();
        }
        break;
    }

    // Qt6：repaint()（同步立即重绘）已删除，用 update()（异步合并重绘），
    // 高频 mouseMove 下还能把多次刷新合并成一次 paint，更流畅
    update();
}

void ImageCropperLabel::mouseReleaseEvent(QMouseEvent *) {
    isLButtonPressed = false;
    isCursorPosCalculated = false;
    setCursor(Qt::ArrowCursor);
}

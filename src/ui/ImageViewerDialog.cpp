#include "ui/ImageViewerDialog.h"

#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace ui {

ImageViewerDialog::ImageViewerDialog(const QString &filePath, QWidget *parent)
    : QDialog(parent)
    , m_filePath(filePath)
    , m_fileName(QFileInfo(filePath).fileName())
{
    setObjectName(QStringLiteral("ImageViewer"));
    setWindowTitle(m_fileName.isEmpty() ? tr("Просмотр изображения") : m_fileName);
    setModal(true);
    resize(960, 720);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);

    m_image = new QLabel(this);
    m_image->setObjectName(QStringLiteral("ViewerImage"));
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setTextInteractionFlags(Qt::NoTextInteraction);
    layout->addWidget(m_image, 1);

    QImageReader probe(filePath);
    m_imageSize = probe.size();

    reload();
}

void ImageViewerDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    reload();
}

void ImageViewerDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void ImageViewerDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void ImageViewerDialog::reload()
{
    if (!m_image)
        return;

    QImageReader reader(m_filePath);
    reader.setAutoTransform(true);

    const QSize original = reader.size();
    QSize target = original;

    if (target.isValid()) {
        QSize available = m_image->size();
        if (available.isEmpty())
            available = QSize(1600, 1200);
        target.scale(available, Qt::KeepAspectRatio);
        reader.setScaledSize(target);
    }

    const QImage image = reader.read();
    if (image.isNull()) {
        m_image->setText(tr("Не удалось открыть изображение"));
        m_imageSize = QSize();
        return;
    }

    m_imageSize = original.isValid() ? original : image.size();
    m_image->setPixmap(QPixmap::fromImage(image));
}

} // namespace ui

#pragma once

#include <QDialog>
#include <QSize>

class QLabel;

namespace ui {

// Полноэкранный просмотр изображения из чата.
class ImageViewerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImageViewerDialog(const QString &filePath, QWidget *parent = nullptr);

    QString filePath() const { return m_filePath; }
    QSize imageSize() const { return m_imageSize; }
    bool hasImage() const { return m_imageSize.isValid(); }

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void reload();

    QString m_filePath;
    QString m_fileName;
    QLabel *m_image = nullptr;
    QSize m_imageSize;
};

} // namespace ui

#include "app/AttachmentService.h"
#include "app/ThemeManager.h"
#include "ui/Formatting.h"
#include "ui/ImageViewerDialog.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QtTest>

class TestMedia : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void detectsFileKinds();
    void formatsFileSize();
    void rejectsOversizedFile();
    void imageViewerLoadsPicture();

private:
    QScopedPointer<QTemporaryDir> m_dir;
};

void TestMedia::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    qApp->setStyleSheet(
        app::ThemeManager::styleSheetFor(app::ThemeManager::Theme::Dark));
}

void TestMedia::cleanup()
{
    m_dir.reset();
}

void TestMedia::detectsFileKinds()
{
    QCOMPARE(app::AttachmentService::kindForFile(QStringLiteral("photo.png")),
             data::MessageKind::Image);
    QCOMPARE(app::AttachmentService::kindForFile(QStringLiteral("anim.gif")),
             data::MessageKind::Gif);
    QCOMPARE(app::AttachmentService::kindForFile(QStringLiteral("clip.mp4")),
             data::MessageKind::Video);
    QCOMPARE(app::AttachmentService::kindForFile(QStringLiteral("report.pdf")),
             data::MessageKind::File);
    QCOMPARE(app::AttachmentService::kindForFile(QStringLiteral("archive.zip")),
             data::MessageKind::File);
}

void TestMedia::formatsFileSize()
{
    QCOMPARE(ui::format::fileSize(512), QStringLiteral("512 Б"));
    QVERIFY(ui::format::fileSize(2048).contains(QStringLiteral("КБ")));
    QVERIFY(ui::format::fileSize(5 * 1024 * 1024).contains(QStringLiteral("МБ")));
}

void TestMedia::rejectsOversizedFile()
{
    const QString path = m_dir->filePath(QStringLiteral("big.bin"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(26 * 1024 * 1024)); // 26 МБ > лимита 25 МБ
    file.close();

    app::AttachmentService service(nullptr, m_dir->path());
    QSignalSpy failed(&service, &app::AttachmentService::uploadFailed);

    service.upload(1, path);

    QCOMPARE(failed.count(), 1);
    QVERIFY2(failed.first().at(2).toString().contains(QStringLiteral("25")),
             qPrintable(failed.first().at(2).toString()));
}

void TestMedia::imageViewerLoadsPicture()
{
    const QString path = m_dir->filePath(QStringLiteral("picture.png"));

    QImage image(96, 64, QImage::Format_ARGB32);
    image.fill(QColor(QStringLiteral("#3B7DF6")));
    QVERIFY(image.save(path));

    ui::ImageViewerDialog viewer(path);
    QVERIFY(viewer.hasImage());
    QVERIFY2(viewer.imageSize() == QSize(96, 64),
             qPrintable(QStringLiteral("actual %1x%2")
                            .arg(viewer.imageSize().width())
                            .arg(viewer.imageSize().height())));

    viewer.resize(720, 540);
    viewer.show();
    QTest::qWait(150);

    const QString shotDir = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("screenshots"));
    QDir().mkpath(shotDir);
    viewer.grab().save(shotDir + QStringLiteral("/viewer-image.png"));

    viewer.close();
}

QTEST_MAIN(TestMedia)
#include "tst_media.moc"

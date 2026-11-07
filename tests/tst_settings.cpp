#include "app/SettingsStore.h"
#include "app/ThemeManager.h"
#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QtTest>

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void themeSwitchAppliesImmediately();
    void languageIsPersisted();
    void notificationsToggleIsPersisted();

private:
    QComboBox *themeBox(ui::SettingsDialog &dialog) const;
    QComboBox *languageBox(ui::SettingsDialog &dialog) const;

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<app::SettingsStore> m_settings;
    QScopedPointer<app::ThemeManager> m_theme;
};

void TestSettings::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    m_settings.reset(new app::SettingsStore(m_dir->filePath(QStringLiteral("settings.ini"))));
    m_theme.reset(new app::ThemeManager(m_settings.data()));
    m_theme->apply();
}

void TestSettings::cleanup()
{
    m_theme.reset();
    m_settings.reset();
    m_dir.reset();
}

QComboBox *TestSettings::themeBox(ui::SettingsDialog &dialog) const
{
    for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
        if (combo->itemData(0).toString() != QLatin1String("auto"))
            return combo;
    }
    return nullptr;
}

QComboBox *TestSettings::languageBox(ui::SettingsDialog &dialog) const
{
    for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
        if (combo->itemData(0).toString() == QLatin1String("auto"))
            return combo;
    }
    return nullptr;
}

void TestSettings::themeSwitchAppliesImmediately()
{
    ui::SettingsDialog dialog(m_settings.data(), m_theme.data());

    QComboBox *theme = themeBox(dialog);
    QVERIFY(theme);
    QCOMPARE(theme->currentIndex(), 0);

    theme->setCurrentIndex(1);

    QCOMPARE(m_theme->theme(), app::ThemeManager::Theme::Dark);
    QCOMPARE(m_settings->theme(), 1);

    theme->setCurrentIndex(0);
    QCOMPARE(m_theme->theme(), app::ThemeManager::Theme::Light);

    dialog.show();
    QTest::qWait(120);
    const QString shotDir = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("screenshots"));
    QDir().mkpath(shotDir);
    dialog.grab().save(shotDir + QStringLiteral("/settings.png"));
}

void TestSettings::languageIsPersisted()
{
    ui::SettingsDialog dialog(m_settings.data(), m_theme.data());
    dialog.show();

    QComboBox *language = languageBox(dialog);
    QVERIFY(language);

    const int englishIndex = language->findData(QStringLiteral("en"));
    QVERIFY(englishIndex >= 0);

    language->setCurrentIndex(englishIndex);

    QCOMPARE(m_settings->language(), QStringLiteral("en"));

    bool hintVisible = false;
    for (QLabel *label : dialog.findChildren<QLabel *>()) {
        if (label->text().contains(QStringLiteral("перезапуск")) && label->isVisible())
            hintVisible = true;
    }
    QVERIFY2(hintVisible, "не показана подсказка о перезапуске");
}

void TestSettings::notificationsToggleIsPersisted()
{
    ui::SettingsDialog dialog(m_settings.data(), m_theme.data());

    auto *checkbox = dialog.findChild<QCheckBox *>();
    QVERIFY(checkbox);
    QVERIFY(checkbox->isChecked());

    checkbox->setChecked(false);
    QVERIFY(!m_settings->notificationsEnabled());

    checkbox->setChecked(true);
    QVERIFY(m_settings->notificationsEnabled());
}

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"

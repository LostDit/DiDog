#pragma once

#include <QString>
#include <QWidget>

namespace ui {

// Круглый аватар: цветной градиент по hue + инициалы.
// Используется и в списке чатов, и в шапке чата, и в профиле.
class AvatarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AvatarWidget(QWidget *parent = nullptr);

    void setIdentity(const QString &displayName, int hue = 210);
    void setDiameter(int diameter);

    int diameter() const { return m_diameter; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static QString initialsFor(const QString &displayName);

    QString m_initials = QStringLiteral("?");
    int m_hue = 210;
    int m_diameter = 40;
};

} // namespace ui

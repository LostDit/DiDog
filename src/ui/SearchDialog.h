#pragma once

#include "data/Models.h"

#include <QDialog>
#include <QHash>
#include <QList>

class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace ui {

// Глобальный поиск по сохранённым сообщениям.
class SearchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SearchDialog(QWidget *parent = nullptr);

    void setResults(const QList<data::Message> &messages,
                    const QHash<qint64, QString> &chatNames);
    void setStatus(const QString &text);

signals:
    void searchRequested(const QString &query);
    void messageActivated(qint64 chatId, qint64 messageId);

private slots:
    void onQueryChanged();
    void onItemActivated();

private:
    QLineEdit *m_query = nullptr;
    QListWidget *m_results = nullptr;
    QLabel *m_status = nullptr;
    QTimer *m_debounce = nullptr;
};

} // namespace ui

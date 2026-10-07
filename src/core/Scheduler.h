#pragma once

#include <QObject>
#include <QDateTime>
#include <QTimer>
#include <array>
#include <utility>

class Scheduler final : public QObject {
    Q_OBJECT
public:
    explicit Scheduler(QObject *parent = nullptr);

    bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled);

    void setDay(int dayOfWeek, bool enabled, int startMinute, int endMinute);
    bool dayEnabled(int dayOfWeek) const;
    int startMinute(int dayOfWeek) const;
    int endMinute(int dayOfWeek) const;

    bool allowedNow(const QDateTime &now = QDateTime::currentDateTime()) const;

signals:
    void scheduleStateChanged(bool allowed);

private slots:
    void tick();

private:
    struct Window { bool enabled = false; int start = 0; int end = 0; };
    void load();
    void save();

    bool enabled_ = false;
    std::array<Window, 7> days_{};
    QTimer timer_;
    bool lastAllowed_ = true;
};

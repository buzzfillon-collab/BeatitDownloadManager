#include "Scheduler.h"

#include <QSettings>

Scheduler::Scheduler(QObject *parent) : QObject(parent) {
    load();
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, &Scheduler::tick);
    timer_.start();
    lastAllowed_ = allowedNow();
}

void Scheduler::load() {
    QSettings s(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    enabled_ = s.value(QStringLiteral("scheduler/enabled"), false).toBool();
    for (int day = 1; day <= 7; ++day) {
        const int i = day - 1;
        days_[i].enabled = s.value(QStringLiteral("scheduler/day%1/enabled").arg(day), false).toBool();
        days_[i].start = qBound(0, s.value(QStringLiteral("scheduler/day%1/start").arg(day), 0).toInt(), 1439);
        days_[i].end = qBound(0, s.value(QStringLiteral("scheduler/day%1/end").arg(day), 1439).toInt(), 1439);
    }
}

void Scheduler::save() {
    QSettings s(QStringLiteral("Beatit"), QStringLiteral("Beatit"));
    s.setValue(QStringLiteral("scheduler/enabled"), enabled_);
    for (int day = 1; day <= 7; ++day) {
        const auto &w = days_[day - 1];
        s.setValue(QStringLiteral("scheduler/day%1/enabled").arg(day), w.enabled);
        s.setValue(QStringLiteral("scheduler/day%1/start").arg(day), w.start);
        s.setValue(QStringLiteral("scheduler/day%1/end").arg(day), w.end);
    }
}

void Scheduler::setEnabled(bool enabled) {
    enabled_ = enabled;
    save();
    const bool allowed = allowedNow();
    if (allowed != lastAllowed_) {
        lastAllowed_ = allowed;
        emit scheduleStateChanged(allowed);
    } else {
        emit scheduleStateChanged(allowed);
    }
}

void Scheduler::setDay(int dayOfWeek, bool enabled, int startMinute, int endMinute) {
    if (dayOfWeek < 1 || dayOfWeek > 7) return;
    auto &w = days_[dayOfWeek - 1];
    w.enabled = enabled;
    w.start = qBound(0, startMinute, 1439);
    w.end = qBound(0, endMinute, 1439);
    save();
    const bool allowed = allowedNow();
    if (allowed != lastAllowed_) {
        lastAllowed_ = allowed;
        emit scheduleStateChanged(allowed);
    }
}

bool Scheduler::dayEnabled(int dayOfWeek) const {
    return dayOfWeek >= 1 && dayOfWeek <= 7 ? days_[dayOfWeek - 1].enabled : false;
}

int Scheduler::startMinute(int dayOfWeek) const {
    return dayOfWeek >= 1 && dayOfWeek <= 7 ? days_[dayOfWeek - 1].start : 0;
}

int Scheduler::endMinute(int dayOfWeek) const {
    return dayOfWeek >= 1 && dayOfWeek <= 7 ? days_[dayOfWeek - 1].end : 1439;
}

bool Scheduler::allowedNow(const QDateTime &now) const {
    if (!enabled_) return true;

    const QTime time = now.time();
    const int minute = time.hour() * 60 + time.minute();
    const int day = now.date().dayOfWeek();

    const auto &today = days_[day - 1];
    if (today.enabled) {
        if (today.start == today.end) return true;
        if (today.start < today.end && minute >= today.start && minute < today.end) return true;
        if (today.start > today.end && minute >= today.start) return true;
    }

    // An overnight window belongs to the previous calendar day after midnight.
    const int previousDay = day == 1 ? 7 : day - 1;
    const auto &previous = days_[previousDay - 1];
    if (previous.enabled && previous.start > previous.end && minute < previous.end) return true;

    return false;
}

void Scheduler::tick() {
    const bool allowed = allowedNow();
    if (allowed == lastAllowed_) return;
    lastAllowed_ = allowed;
    emit scheduleStateChanged(allowed);
}

#include "event_bus.h"

EventBus& EventBus::instance() {
    static EventBus inst;
    return inst;
}

EventBus::EventBus(QObject *parent) : QObject(parent) {}


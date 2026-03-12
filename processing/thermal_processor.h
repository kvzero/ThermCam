#ifndef THERMAL_PROCESSOR_H
#define THERMAL_PROCESSOR_H

#include <QObject>
#include <QSize>
#include "core/types.h" // 包含 RawFrame 和 VisualFrame

class ThermalProcessor : public QObject {
    Q_OBJECT
public:
    explicit ThermalProcessor(QObject *parent = nullptr);

    // 外部告诉处理器：屏幕/窗口现在多大
    void setTargetSize(const QSize& size);

public slots:
    // 接收驱动的原始帧
    void processFrame(const RawFrame& raw);

signals:
    // 发送处理好的成品帧给 UI
    void frameReady(const VisualFrame& frame);

private:
    QSize m_targetSize; // 目标输出尺寸
};

#endif // THERMAL_PROCESSOR_H

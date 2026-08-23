#ifndef OPERATION_SERVICE_H
#define OPERATION_SERVICE_H

#include "hardware/storage/storage_manager.h"

#include <QObject>
#include <optional>

enum class OperationID : quint8 {
    FlatSceneCorrection,
    FormatSdCard,
    FormatUsbDisk
};

enum class OperationStartCode : quint8 {
    Started,
    Busy,
    Rejected
};

/**
 * @brief Owns the single in-flight blocking operation for the application.
 *
 * Concrete execution and thread policy remain in each hardware owner. This
 * service owns only global exclusion and the shared operation lifecycle.
 */
class OperationService final : public QObject {
    Q_OBJECT
public:
    static OperationService& instance();

    bool isBusy() const { return m_currentOperation.has_value(); }

    OperationStartCode startFlatSceneCorrection();
    OperationStartCode startFormatVolume(StorageVolume volume);

signals:
    void operationStarted(OperationID operation);
    void operationFinished(OperationID operation, bool success);

private:
    explicit OperationService(QObject* parent = nullptr);

    void complete(OperationID operation, bool success);

    std::optional<OperationID> m_currentOperation;
};

#endif // OPERATION_SERVICE_H

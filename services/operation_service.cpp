#include "services/operation_service.h"

#include "hardware/hardware_manager.h"
#include "hardware/imaging/thermal_camera.h"

namespace {
OperationID formatOperation(StorageVolume volume) {
    switch (volume) {
    case StorageVolume::SdCard:
        return OperationID::FormatSdCard;
    case StorageVolume::UsbDisk:
        return OperationID::FormatUsbDisk;
    case StorageVolume::Nand:
        Q_UNREACHABLE();
    }
    Q_UNREACHABLE();
}
}

OperationService& OperationService::instance() {
    static OperationService inst;
    return inst;
}

OperationService::OperationService(QObject* parent) : QObject(parent) {
    if (auto* storage = HardwareManager::instance().storage()) {
        connect(storage, &StorageManager::formatVolumeFinished, this,
                [this](StorageVolume volume, bool success) {
                    complete(formatOperation(volume), success);
                });
    }
    if (auto* camera = HardwareManager::instance().camera()) {
        connect(camera, &ThermalCamera::flatSceneCorrectionFinished, this,
                [this](bool success) {
                    complete(OperationID::FlatSceneCorrection, success);
                });
    }
}

OperationStartCode OperationService::startFlatSceneCorrection() {
    if (isBusy()) return OperationStartCode::Busy;

    auto* camera = HardwareManager::instance().camera();
    if (!camera) return OperationStartCode::Rejected;

    const OperationID operation = OperationID::FlatSceneCorrection;
    m_currentOperation = operation;
    if (!camera->startFlatSceneCorrection()) {
        m_currentOperation.reset();
        return OperationStartCode::Rejected;
    }
    emit operationStarted(operation);
    return OperationStartCode::Started;
}

OperationStartCode OperationService::startFormatVolume(StorageVolume volume) {
    if (isBusy()) return OperationStartCode::Busy;
    if (volume != StorageVolume::SdCard && volume != StorageVolume::UsbDisk) {
        return OperationStartCode::Rejected;
    }

    auto* storage = HardwareManager::instance().storage();
    if (!storage) return OperationStartCode::Rejected;

    if (!storage->startFormatVolume(volume)) return OperationStartCode::Rejected;

    const OperationID operation = formatOperation(volume);
    m_currentOperation = operation;
    emit operationStarted(operation);
    return OperationStartCode::Started;
}

void OperationService::complete(OperationID operation, bool success) {
    if (!m_currentOperation.has_value() || *m_currentOperation != operation) return;

    m_currentOperation.reset();
    emit operationFinished(operation, success);
}

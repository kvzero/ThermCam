#include "core/settings_store.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
constexpr int kSchemaVersion = 1;
const char* kEnvConfigPath = "THERMAL_QT_CONFIG_FILE";
const char* kDefaultConfigPath = "/root/.config/thermal_qt/settings.json";

QString keyToJsonName(SettingKey key) {
    for (const auto& descriptor : kSettingRegistry) {
        if (descriptor.key == key) {
            return QString::fromLatin1(descriptor.jsonName);
        }
    }
    return QString();
}

bool jsonNameToKey(const QString& name, SettingKey* outKey) {
    if (!outKey) return false;
    for (const auto& descriptor : kSettingRegistry) {
        if (name == QLatin1String(descriptor.jsonName)) {
            *outKey = descriptor.key;
            return true;
        }
    }
    return false;
}
} // namespace

SettingsStore& SettingsStore::instance() {
    static SettingsStore inst;
    return inst;
}

SettingsStore::SettingsStore(QObject* parent) : QObject(parent) {
    for (const auto& descriptor : kSettingRegistry) {
        m_defaults.insert(descriptor.key, descriptor.defaultValue);
    }
}

bool SettingsStore::init(const QString& customPath) {
    QMutexLocker commitGuard(&m_commitMutex);

    if (m_isInitialized) return true;

    QString error;
    const QString path = resolveStoragePath(customPath);
    if (path.isEmpty()) return false;

    if (!ensureParentDirectory(path, &error)) {
        qWarning() << "[SettingsStore] init failed:" << error;
        return false;
    }

    SettingsSnapshot loaded = buildDefaultSnapshot();

    QFileInfo info(path);
    if (info.exists()) {
        if (!loadFromDisk(path, &loaded, &error)) {
            const QString backup = path + ".bad." +
                                   QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
            QFile::rename(path, backup);

            loaded = buildDefaultSnapshot();
            if (!writeToDisk(path, loaded, &error)) {
                qWarning() << "[SettingsStore] repair write failed:" << error;
                return false;
            }
        }
    } else {
        if (!writeToDisk(path, loaded, &error)) {
            qWarning() << "[SettingsStore] first boot write failed:" << error;
            return false;
        }
    }

    {
        QWriteLocker lock(&m_stateLock);
        m_snapshot = loaded;
        m_storagePath = path;
        m_isInitialized = true;
    }

    qInfo() << "[SettingsStore] initialized at" << path;
    return true;
}

SettingsSnapshot SettingsStore::current() const {
    QReadLocker lock(&m_stateLock);
    return m_snapshot;
}

bool SettingsStore::commitPatch(const SettingsPatch& patch,
                                SettingsChangeEvent* outChange,
                                QString* outError) {
    QMutexLocker commitGuard(&m_commitMutex);

    if (!m_isInitialized) {
        if (outError) *outError = "SettingsStore is not initialized";
        return false;
    }

    if (patch.isEmpty()) {
        if (outChange) {
            outChange->snapshot = current();
            outChange->changedKeys.clear();
        }
        return true;
    }

    SettingsSnapshot next;
    {
        QReadLocker lock(&m_stateLock);
        next = m_snapshot;
    }

    QSet<SettingKey> changed;

    for (auto it = patch.values.constBegin(); it != patch.values.constEnd(); ++it) {
        const SettingKey key = it.key();
        if (!m_defaults.contains(key)) {
            if (outError) {
                *outError = QString("Unsupported setting key: %1")
                                .arg(static_cast<int>(static_cast<quint8>(key)));
            }
            return false;
        }

        const QVariant defaultValue = m_defaults.value(key);

        QVariant oldValue = next.values.value(key, defaultValue);
        if (!oldValue.convert(defaultValue.userType())) {
            oldValue = defaultValue;
        }

        QVariant newValue = it.value();
        if (!newValue.convert(defaultValue.userType())) {
            if (outError) {
                *outError = QString("Type mismatch for setting key: %1")
                                .arg(static_cast<int>(static_cast<quint8>(key)));
            }
            return false;
        }

        const bool isSame = (oldValue == newValue);

        if (!isSame) {
            next.values.insert(key, newValue);
            changed.insert(key);
        }
    }

    if (changed.isEmpty()) {
        if (outChange) {
            outChange->snapshot = next;
            outChange->changedKeys.clear();
        }
        return true;
    }

    next.revision += 1;

    QString error;
    if (!writeToDisk(m_storagePath, next, &error)) {
        if (outError) *outError = error;
        return false;
    }

    {
        QWriteLocker lock(&m_stateLock);
        m_snapshot = next;
    }

    SettingsChangeEvent event;
    event.snapshot = next;
    event.changedKeys = changed;

    if (outChange) {
        *outChange = event;
    }

    commitGuard.unlock();

    emit settingsChanged(event);
    return true;
}

QString SettingsStore::resolveStoragePath(const QString& customPath) const {
    if (!customPath.trimmed().isEmpty()) {
        return customPath;
    }

    const QByteArray envPath = qgetenv(kEnvConfigPath);
    if (!envPath.trimmed().isEmpty()) {
        return QString::fromUtf8(envPath);
    }

    return QString::fromLatin1(kDefaultConfigPath);
}

bool SettingsStore::ensureParentDirectory(const QString& filePath, QString* outError) const {
    QFileInfo info(filePath);
    QDir parent = info.dir();

    if (parent.exists()) return true;
    if (parent.mkpath(".")) return true;

    if (outError) {
        *outError = QString("Failed to create directory: %1").arg(parent.absolutePath());
    }
    return false;
}

bool SettingsStore::loadFromDisk(const QString& filePath,
                                 SettingsSnapshot* outSnapshot,
                                 QString* outError) const {
    if (!outSnapshot) {
        if (outError) *outError = "Null output pointer";
        return false;
    }

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outError) *outError = QString("Failed to open file: %1").arg(filePath);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (outError) {
            *outError = QString("Invalid JSON format: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = doc.object();
    if (root.value("schema_version").toInt(-1) != kSchemaVersion) {
        if (outError) *outError = "Unsupported schema version";
        return false;
    }

    SettingsSnapshot snapshot = buildDefaultSnapshot();
    snapshot.revision = static_cast<quint64>(root.value("revision").toDouble(0.0));

    const QJsonObject valuesObj = root.value("values").toObject();
    for (auto it = valuesObj.constBegin(); it != valuesObj.constEnd(); ++it) {
        SettingKey key;
        if (!jsonNameToKey(it.key(), &key)) continue;

        QVariant defaultValue = m_defaults.value(key);
        QVariant parsedValue = it.value().toVariant();

        if (parsedValue.convert(defaultValue.userType())) {
            snapshot.values.insert(key, parsedValue);
        } else {
            snapshot.values.insert(key, defaultValue);
        }
    }

    *outSnapshot = snapshot;
    return true;
}

bool SettingsStore::writeToDisk(const QString& filePath,
                                const SettingsSnapshot& snapshot,
                                QString* outError) const {
    QJsonObject valuesObj;

    for (auto it = snapshot.values.constBegin(); it != snapshot.values.constEnd(); ++it) {
        const QString key = keyToJsonName(it.key());
        if (!key.isEmpty()) {
            valuesObj.insert(key, QJsonValue::fromVariant(it.value())); 
        }
    }

    QJsonObject root;
    root.insert("schema_version", kSchemaVersion);
    root.insert("revision", static_cast<double>(snapshot.revision));
    root.insert("values", valuesObj);

    const QJsonDocument doc(root);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (outError) *outError = QString("Failed to open QSaveFile: %1").arg(filePath);
        return false;
    }

    if (file.write(doc.toJson(QJsonDocument::Indented)) < 0) {
        if (outError) *outError = QString("Failed to write JSON payload: %1").arg(filePath);
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        if (outError) *outError = QString("Failed to commit QSaveFile: %1").arg(filePath);
        return false;
    }

    return true;
}

SettingsSnapshot SettingsStore::buildDefaultSnapshot() const {
    SettingsSnapshot snapshot;
    snapshot.values = m_defaults;
    snapshot.revision = 0;
    return snapshot;
}

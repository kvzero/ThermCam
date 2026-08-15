#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <QObject>
#include <QReadWriteLock>
#include <QMutex>
#include <QString>
#include "core/settings_types.h"

class SettingsService;

/**
 * @brief Persistent owner for settings truth snapshot and storage lifecycle.
 *
 * Architectural Role:
 * - Sole owner of committed settings values and revision sequence.
 * - Single translator between enum-based SettingKey and JSON key strings.
 *
 * Owner Scope:
 * - Holds in-memory snapshot cache and file persistence logic.
 * - Accepts mutations only from SettingsService.
 *
 * Lifecycle:
 * - Process-wide singleton.
 * - Bootstrapped once during application startup.
 */
class SettingsStore : public QObject {
    Q_OBJECT
public:
    /* --- Lifecycle --- */

    static SettingsStore& instance();

    /**
     * @brief Loads settings from disk and ensures a valid committed snapshot exists.
     *
     * Contract:
     * - Missing file creates a default snapshot and persists it.
     * - Malformed file is quarantined and replaced by defaults.
     */
    bool init(const QString& customPath = QString());

    /* --- Public Read API --- */

    SettingsSnapshot current() const;

signals:
    /**
     * @brief Emitted once per successful commit with only touched key set.
     */
    void settingsChanged(const SettingsChangeEvent& change);

private:
    friend class SettingsService;

    explicit SettingsStore(QObject* parent = nullptr);

    /* --- Service-Owned Commit API --- */

    bool commitPatch(const SettingsPatch& patch,
                     SettingsChangeEvent* outChange,
                     QString* outError);

    /* --- Persistence Engine --- */

    QString resolveStoragePath(const QString& customPath) const;
    bool ensureParentDirectory(const QString& filePath, QString* outError) const;
    bool loadFromDisk(const QString& filePath, SettingsSnapshot* outSnapshot, QString* outError) const;
    bool writeToDisk(const QString& filePath, const SettingsSnapshot& snapshot, QString* outError) const;

    /* --- Snapshot Helpers --- */

    SettingsSnapshot buildDefaultSnapshot() const;

    /* --- Runtime State --- */

    mutable QReadWriteLock m_stateLock;
    QMutex m_commitMutex;

    SettingsSnapshot m_snapshot;
    QString m_storagePath;
    bool m_isInitialized = false;
};

#endif // SETTINGS_STORE_H

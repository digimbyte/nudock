#include "dock_restore_coordinator.hpp"

#include <QSet>

#include <algorithm>
#include <utility>

namespace {
constexpr int PollIntervalMs = 250;
constexpr int GraceWindowMs = 5000;
constexpr int FinalPoll = GraceWindowMs / PollIntervalMs;

QStringList missingFrom(const QStringList &expected,
                        const QStringList &inventory) {
  const QSet<QString> present(inventory.begin(), inventory.end());
  QStringList missing;
  for (const QString &id : expected) {
    if (!present.contains(id))
      missing.push_back(id);
  }
  return missing;
}
} // namespace

DockRestoreCoordinator::DockRestoreCoordinator(
    CurrentProfileProvider currentProfileProvider,
    MappingProvider mappingProvider, SnapshotProvider snapshotProvider,
    RestoreCallback restoreCallback,
    DockInventoryProvider dockInventoryProvider,
    RestoreLateDocksCallback restoreLateDocksCallback,
    ScheduleCallback scheduleCallback,
    ErrorCallback errorCallback)
    : currentProfileProvider_(std::move(currentProfileProvider)),
      mappingProvider_(std::move(mappingProvider)),
      snapshotProvider_(std::move(snapshotProvider)),
      restoreCallback_(std::move(restoreCallback)),
      dockInventoryProvider_(std::move(dockInventoryProvider)),
      restoreLateDocksCallback_(std::move(restoreLateDocksCallback)),
      scheduleCallback_(std::move(scheduleCallback)),
      errorCallback_(std::move(errorCallback)) {}

void DockRestoreCoordinator::scheduleForCurrentProfile() {
  const QString obsProfileName = currentProfileProvider_();
  const QString dockProfileId = mappingProvider_(obsProfileName);
  const std::uint64_t generation = ++generation_;
  reportedErrorGeneration_ = 0;
  if (obsProfileName.isEmpty() || dockProfileId.isEmpty())
    return;

  scheduleCallback_(0, [this, generation, obsProfileName, dockProfileId]() {
    restoreInitial(generation, obsProfileName, dockProfileId);
  });
}

void DockRestoreCoordinator::cancel() {
  ++generation_;
  reportedErrorGeneration_ = 0;
}

bool DockRestoreCoordinator::isCurrent(std::uint64_t generation,
                                       const QString &obsProfileName,
                                       const QString &dockProfileId) const {
  return generation == generation_ &&
         currentProfileProvider_() == obsProfileName &&
         mappingProvider_(obsProfileName) == dockProfileId;
}

void DockRestoreCoordinator::restoreInitial(std::uint64_t generation,
                                            const QString &obsProfileName,
                                            const QString &dockProfileId) {
  if (!isCurrent(generation, obsProfileName, dockProfileId))
    return;

  const std::optional<DockRestoreSnapshot> snapshot =
      snapshotProvider_(dockProfileId);
  if (!snapshot) {
    reportOnce(generation,
               QStringLiteral("Mapped Dock Profile '%1' is unavailable.")
                   .arg(dockProfileId));
    return;
  }

  QString error;
  if (!restoreCallback_(snapshot->state, snapshot->qtStateVersion, &error)) {
    reportOnce(generation,
               QStringLiteral("Could not restore Dock Profile '%1': %2")
                   .arg(snapshot->name, error));
    return;
  }

  const QStringList unresolved =
      missingFrom(snapshot->dockIds, dockInventoryProvider_());
  if (unresolved.isEmpty())
    return;
  scheduleCallback_(PollIntervalMs,
                    [this, generation, obsProfileName, dockProfileId,
                     snapshot = *snapshot, unresolved]() mutable {
                      pollInventory(generation, obsProfileName, dockProfileId,
                                    std::move(snapshot), unresolved, 1, false);
                    });
}

void DockRestoreCoordinator::pollInventory(
    std::uint64_t generation, const QString &obsProfileName,
    const QString &dockProfileId, DockRestoreSnapshot snapshot,
    QStringList unresolvedDockIds, int pollNumber, bool fallbackUsed) {
  if (!isCurrent(generation, obsProfileName, dockProfileId))
    return;

  const QStringList stillMissing =
      missingFrom(unresolvedDockIds, dockInventoryProvider_());
  QSet<QString> stillMissingSet(stillMissing.begin(), stillMissing.end());
  QStringList appeared;
  for (const QString &id : unresolvedDockIds) {
    if (!stillMissingSet.contains(id))
      appeared.push_back(id);
  }

  if (!appeared.isEmpty()) {
    QString error;
    if (!restoreLateDocksCallback_(appeared, &error)) {
      if (!fallbackUsed) {
        fallbackUsed = true;
        QString fallbackError;
        if (!restoreCallback_(snapshot.state, snapshot.qtStateVersion,
                              &fallbackError)) {
          reportOnce(generation,
                     QStringLiteral("Could not restore late docks for Dock Profile '%1': %2; fallback failed: %3")
                         .arg(snapshot.name, error, fallbackError));
          return;
        }
      } else {
        reportOnce(generation,
                   QStringLiteral("Could not restore late docks for Dock Profile '%1': %2")
                       .arg(snapshot.name, error));
      }
    }
  }

  if (stillMissing.isEmpty())
    return;
  if (pollNumber >= FinalPoll) {
    errorCallback_(
        QStringLiteral("Dock Profile '%1' could not find expected docks: %2")
            .arg(snapshot.name, stillMissing.join(QStringLiteral(", "))));
    return;
  }

  scheduleCallback_(PollIntervalMs,
                    [this, generation, obsProfileName, dockProfileId,
                     snapshot = std::move(snapshot), stillMissing, pollNumber,
                     fallbackUsed]() mutable {
                      pollInventory(generation, obsProfileName, dockProfileId,
                                    std::move(snapshot), stillMissing,
                                    pollNumber + 1, fallbackUsed);
                    });
}

void DockRestoreCoordinator::reportOnce(std::uint64_t generation,
                                        const QString &message) {
  if (reportedErrorGeneration_ == generation)
    return;
  reportedErrorGeneration_ = generation;
  errorCallback_(message);
}

#include "dock_restore_coordinator.hpp"

#include <array>
#include <utility>

DockRestoreCoordinator::DockRestoreCoordinator(
    CurrentProfileProvider currentProfileProvider,
    MappingProvider mappingProvider, SnapshotProvider snapshotProvider,
    RestoreCallback restoreCallback, ScheduleCallback scheduleCallback,
    ErrorCallback errorCallback)
    : currentProfileProvider_(std::move(currentProfileProvider)),
      mappingProvider_(std::move(mappingProvider)),
      snapshotProvider_(std::move(snapshotProvider)),
      restoreCallback_(std::move(restoreCallback)),
      scheduleCallback_(std::move(scheduleCallback)),
      errorCallback_(std::move(errorCallback)) {}

void DockRestoreCoordinator::scheduleForCurrentProfile() {
  const QString obsProfileName = currentProfileProvider_();
  const QString dockProfileId = mappingProvider_(obsProfileName);
  const std::uint64_t generation = ++generation_;
  reportedErrorGeneration_ = 0;
  if (obsProfileName.isEmpty() || dockProfileId.isEmpty())
    return;

  constexpr std::array<int, 3> delays{0, 250, 1000};
  for (const int delay : delays) {
    scheduleCallback_(delay,
                      [this, generation, obsProfileName, dockProfileId]() {
                        attempt(generation, obsProfileName, dockProfileId);
                      });
  }
}

void DockRestoreCoordinator::cancel() {
  ++generation_;
  reportedErrorGeneration_ = 0;
}

void DockRestoreCoordinator::attempt(std::uint64_t generation,
                                     const QString &obsProfileName,
                                     const QString &dockProfileId) {
  if (generation != generation_ ||
      currentProfileProvider_() != obsProfileName ||
      mappingProvider_(obsProfileName) != dockProfileId)
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
  }
}

void DockRestoreCoordinator::reportOnce(std::uint64_t generation,
                                        const QString &message) {
  if (reportedErrorGeneration_ == generation)
    return;
  reportedErrorGeneration_ = generation;
  errorCallback_(message);
}

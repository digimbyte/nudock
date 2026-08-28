#include "dock_state_restorer.hpp"

#include <QMainWindow>

bool restoreDockStateTransactional(QMainWindow &window,
                                   const QByteArray &desiredState,
                                   int stateVersion, QString *error) {
  if (desiredState.isEmpty()) {
    if (error)
      *error = QStringLiteral("The Dock Profile contains an empty state.");
    return false;
  }

  const QByteArray previousState = window.saveState(stateVersion);
  if (window.restoreState(desiredState, stateVersion))
    return true;

  if (!previousState.isEmpty())
    window.restoreState(previousState, stateVersion);
  if (error) {
    *error = QStringLiteral(
        "Qt rejected the Dock Profile state; the previous layout was restored.");
  }
  return false;
}

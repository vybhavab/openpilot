#pragma once

#include "selfdrive/ui/qt/offroad/settings.h"
#include "selfdrive/ui/qt/widgets/controls.h"

class DeveloperPanel : public ListWidget {
  Q_OBJECT
public:
  explicit DeveloperPanel(QWidget* parent = nullptr);
  void showEvent(QShowEvent *event) override;

private:
  ButtonControl *showTerminalBtn;
  Params params;
  ParamControl* adbToggle;
  ParamControl* joystickToggle;
  ParamControl* longManeuverToggle;
  ParamControl* experimentalLongitudinalToggle;
  bool is_release;
  bool offroad = false;

private slots:
  void updateToggles(bool _offroad);
};

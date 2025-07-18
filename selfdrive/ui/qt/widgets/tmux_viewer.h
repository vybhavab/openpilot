#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QScrollBar>

class TmuxViewer : public QWidget {
  Q_OBJECT

public:
  explicit TmuxViewer(QWidget *parent = nullptr);
  ~TmuxViewer();

  void connectToSession(const QString &session_name = "default");
  void disconnectFromSession();
  bool isConnected() const { return connected; }

public slots:
  void refreshContent();
  void toggleConnection();

private slots:
  void updateContent();

private:
  void setupUI();
  void setConnected(bool state);
  QString runCommand(const QString &command);

  QVBoxLayout *main_layout;
  QHBoxLayout *control_layout;
  QLabel *status_label;
  QPushButton *connect_btn;
  QPushButton *refresh_btn;
  QTextEdit *terminal_display;
  
  QTimer *refresh_timer;
  
  QString current_session;
  bool connected;
  
  static const int REFRESH_INTERVAL_MS = 1000; // 1 second refresh rate
};
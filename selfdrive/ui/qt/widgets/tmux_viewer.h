#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QScrollBar>
#include <QThread>
#include <QMutex>

class TmuxCaptureThread : public QThread {
  Q_OBJECT

public:
  TmuxCaptureThread(const QString &session_name, QObject *parent = nullptr);
  void stop();

signals:
  void contentReady(const QString &content);

protected:
  void run() override;

private:
  QString session_name;
  bool should_stop;
  QMutex mutex;
  QString runCommand(const QString &command);
};

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
  void toggleFullscreen();

private slots:
  void updateContent(const QString &content);

private:
  void setupUI();
  void setConnected(bool state);
  QString runCommand(const QString &command);

  QVBoxLayout *main_layout;
  QHBoxLayout *control_layout;
  QLabel *status_label;
  QPushButton *connect_btn;
  QPushButton *refresh_btn;
  QPushButton *fullscreen_btn;
  QPushButton *close_btn;
  QTextEdit *terminal_display;
  
  TmuxCaptureThread *capture_thread;
  
  QString current_session;
  bool connected;
  bool is_fullscreen;
  
  static const int REFRESH_INTERVAL_MS = 1000; // 1 second refresh rate
};
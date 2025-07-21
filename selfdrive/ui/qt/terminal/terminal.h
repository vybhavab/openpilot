#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QSocketNotifier>

class Terminal : public QWidget {
  Q_OBJECT

public:
  explicit Terminal(QWidget *parent = nullptr);
  ~Terminal();

private slots:
  void readData();

private:
  bool startPty();
  void write(const QByteArray &data);

  QPlainTextEdit *output;
  int master_fd = -1;
  pid_t child_pid = -1;
  QSocketNotifier *notifier = nullptr;
};
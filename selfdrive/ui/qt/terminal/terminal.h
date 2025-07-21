#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QProcess>

class Terminal : public QWidget {
  Q_OBJECT

public:
  explicit Terminal(QWidget *parent = nullptr);

private slots:
  void handleReadyReadStandardOutput();
  void handleReadyReadStandardError();
  void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
  void startShell();
  void write(const QByteArray &data);

  QPlainTextEdit *output;
  QProcess *process;
};
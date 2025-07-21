#include "selfdrive/ui/qt/terminal/terminal.h"

#include <QVBoxLayout>

Terminal::Terminal(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  output = new QPlainTextEdit(this);
  output->setReadOnly(true);
  output->document()->setMaximumBlockCount(1000);
  output->setStyleSheet(R"(
    QPlainTextEdit {
      background-color: black;
      color: white;
      font-family: "Monospace";
    }
  )");
  layout->addWidget(output);

  process = new QProcess(this);
  connect(process, &QProcess::readyReadStandardOutput, this, &Terminal::handleReadyReadStandardOutput);
  connect(process, &QProcess::readyReadStandardError, this, &Terminal::handleReadyReadStandardError);
  connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &Terminal::handleProcessFinished);

  startShell();
}

void Terminal::startShell() {
  if (process->state() == QProcess::NotRunning) {
    process->start("/bin/sh");
  }
}

void Terminal::handleReadyReadStandardOutput() {
  write(process->readAllStandardOutput());
}

void Terminal::handleReadyReadStandardError() {
  write(process->readAllStandardError());
}

void Terminal::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
  QString status_str = (exitStatus == QProcess::NormalExit) ? "normally" : "crashed";
  write(QString("\n[process terminated %1 with exit code %2]").arg(status_str, QString::number(exitCode)).toUtf8());
}

void Terminal::write(const QByteArray &data) {
  output->moveCursor(QTextCursor::End);
  output->insertPlainText(QString::fromUtf8(data));
  output->moveCursor(QTextCursor::End);
}
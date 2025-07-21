#include "selfdrive/ui/qt/terminal/terminal.h"

#include <QVBoxLayout>
#include <pty.h>
#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>

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

  if (!startPty()) {
    write("Failed to start PTY\n");
  }
}

Terminal::~Terminal() {
  if (child_pid > 0) {
    kill(child_pid, SIGTERM);
    waitpid(child_pid, NULL, 0);
  }
  if (master_fd != -1) {
    close(master_fd);
  }
}

bool Terminal::startPty() {
  char pts_name[256];
  child_pid = forkpty(&master_fd, pts_name, NULL, NULL);

  if (child_pid < 0) {
    return false;
  } else if (child_pid == 0) {
    // Child process
    if (execlp("/bin/sh", "/bin/sh", NULL) < 0) {
      perror("execlp failed");
      _exit(1);
    }
  } else {
    // Parent process
    write(QString("PTY started: %1\n").arg(pts_name).toUtf8());
    notifier = new QSocketNotifier(master_fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &Terminal::readData);
  }
  return true;
}

void Terminal::readData() {
  char buf[4096];
  ssize_t n = read(master_fd, buf, sizeof(buf));
  if (n > 0) {
    write(QByteArray(buf, n));
  } else {
    // PTY closed
    notifier->setEnabled(false);
    write("\n[process terminated]");
  }
}

void Terminal::write(const QByteArray &data) {
  output->moveCursor(QTextCursor::End);
  output->insertPlainText(QString::fromUtf8(data));
  output->moveCursor(QTextCursor::End);
}
#include "selfdrive/ui/qt/widgets/tmux_viewer.h"

#include <QFont>
#include <QFontMetrics>
#include <QDebug>
#include <cstdlib>
#include <fstream>
#include <sstream>

TmuxViewer::TmuxViewer(QWidget *parent) 
    : QWidget(parent), connected(false), current_session("default") {
  setupUI();
  
  refresh_timer = new QTimer(this);
  connect(refresh_timer, &QTimer::timeout, this, &TmuxViewer::updateContent);
}

TmuxViewer::~TmuxViewer() {
  disconnectFromSession();
}

void TmuxViewer::setupUI() {
  main_layout = new QVBoxLayout(this);
  main_layout->setSpacing(10);
  main_layout->setMargin(20);
  
  // Control panel
  control_layout = new QHBoxLayout();
  
  status_label = new QLabel("Disconnected", this);
  status_label->setStyleSheet("font-size: 24px; font-weight: bold; color: #E4E4E4;");
  
  connect_btn = new QPushButton("Connect", this);
  connect_btn->setStyleSheet(R"(
    QPushButton {
      border-radius: 25px;
      font-size: 20px;
      font-weight: 500;
      height: 50px;
      padding: 0 25 0 25;
      color: #E4E4E4;
      background-color: #393939;
    }
    QPushButton:pressed {
      background-color: #4a4a4a;
    }
    QPushButton:disabled {
      color: #33E4E4E4;
      background-color: #2a2a2a;
    }
  )");
  connect(connect_btn, &QPushButton::clicked, this, &TmuxViewer::toggleConnection);
  
  refresh_btn = new QPushButton("Refresh", this);
  refresh_btn->setStyleSheet(connect_btn->styleSheet());
  refresh_btn->setEnabled(false);
  connect(refresh_btn, &QPushButton::clicked, this, &TmuxViewer::refreshContent);
  
  control_layout->addWidget(status_label);
  control_layout->addStretch();
  control_layout->addWidget(refresh_btn);
  control_layout->addWidget(connect_btn);
  
  main_layout->addLayout(control_layout);
  
  // Terminal display
  terminal_display = new QTextEdit(this);
  terminal_display->setReadOnly(true);
  terminal_display->setStyleSheet(R"(
    QTextEdit {
      background-color: #1a1a1a;
      color: #E4E4E4;
      border: 2px solid #393939;
      border-radius: 10px;
      padding: 10px;
      font-family: 'Courier New', monospace;
      font-size: 14px;
      line-height: 1.2;
    }
  )");
  
  // Set monospace font
  QFont font("Courier New", 14);
  font.setStyleHint(QFont::TypeWriter);
  terminal_display->setFont(font);
  
  main_layout->addWidget(terminal_display);
  
  setLayout(main_layout);
}

void TmuxViewer::connectToSession(const QString &session_name) {
  if (connected) {
    disconnectFromSession();
  }
  
  current_session = session_name;
  
  // Check if tmux session exists, create if it doesn't
  QString check_cmd = QString("tmux has-session -t %1 2>/dev/null").arg(current_session);
  int result = std::system(check_cmd.toStdString().c_str());
  
  if (result != 0) {
    // Session doesn't exist, create it
    QString create_cmd = QString("tmux new-session -d -s %1").arg(current_session);
    int create_result = std::system(create_cmd.toStdString().c_str());
    
    if (create_result != 0) {
      status_label->setText("Failed to create session");
      status_label->setStyleSheet("font-size: 24px; font-weight: bold; color: #ff4444;");
      return;
    }
  }
  
  setConnected(true);
  refresh_timer->start(REFRESH_INTERVAL_MS);
  updateContent();
}

void TmuxViewer::disconnectFromSession() {
  if (refresh_timer->isActive()) {
    refresh_timer->stop();
  }
  
  setConnected(false);
}

void TmuxViewer::setConnected(bool state) {
  connected = state;
  
  if (connected) {
    status_label->setText(QString("Connected to: %1").arg(current_session));
    status_label->setStyleSheet("font-size: 24px; font-weight: bold; color: #33Ab4C;");
    connect_btn->setText("Disconnect");
    refresh_btn->setEnabled(true);
  } else {
    status_label->setText("Disconnected");
    status_label->setStyleSheet("font-size: 24px; font-weight: bold; color: #E4E4E4;");
    connect_btn->setText("Connect");
    refresh_btn->setEnabled(false);
    terminal_display->clear();
  }
}

void TmuxViewer::toggleConnection() {
  if (connected) {
    disconnectFromSession();
  } else {
    connectToSession(current_session);
  }
}

void TmuxViewer::refreshContent() {
  if (connected) {
    updateContent();
  }
}

void TmuxViewer::updateContent() {
  if (!connected) {
    return;
  }
  
  // Capture tmux pane content
  QString capture_cmd = QString("tmux capture-pane -t %1 -p").arg(current_session);
  QString output = runCommand(capture_cmd);
  
  if (!output.isEmpty()) {
    // Store current scroll position
    QScrollBar *scrollBar = terminal_display->verticalScrollBar();
    bool wasAtBottom = (scrollBar->value() == scrollBar->maximum());
    
    // Update content
    terminal_display->setPlainText(output);
    
    // Restore scroll position or scroll to bottom if we were there
    if (wasAtBottom) {
      scrollBar->setValue(scrollBar->maximum());
    }
  } else {
    // Handle error - session might have been closed
    if (connected) {
      terminal_display->append("\n[ERROR: Failed to capture tmux content - session may have ended]");
      // Don't disconnect automatically, let user decide
    }
  }
}

QString TmuxViewer::runCommand(const QString &command) {
  // Create a temporary file to capture output
  QString temp_file = "/tmp/tmux_output.txt";
  QString full_cmd = command + " > " + temp_file + " 2>/dev/null";
  
  int result = std::system(full_cmd.toStdString().c_str());
  
  if (result == 0) {
    // Read the output from the temporary file
    std::ifstream file(temp_file.toStdString());
    if (file.is_open()) {
      std::stringstream buffer;
      buffer << file.rdbuf();
      file.close();
      
      // Clean up temp file
      std::system(("rm -f " + temp_file).toStdString().c_str());
      
      return QString::fromStdString(buffer.str());
    }
  }
  
  // Clean up temp file on error
  std::system(("rm -f " + temp_file).toStdString().c_str());
  return QString();
}
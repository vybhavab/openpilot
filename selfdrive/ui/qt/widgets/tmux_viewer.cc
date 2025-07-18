#include "selfdrive/ui/qt/widgets/tmux_viewer.h"

#include <QFont>
#include <QFontMetrics>
#include <QDebug>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QTransform>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#endif

#include "system/hardware/hw.h"

// TmuxCaptureThread implementation
TmuxCaptureThread::TmuxCaptureThread(const QString &session_name, QObject *parent)
    : QThread(parent), session_name(session_name), should_stop(false) {
}

void TmuxCaptureThread::stop() {
  QMutexLocker locker(&mutex);
  should_stop = true;
}

void TmuxCaptureThread::run() {
  while (true) {
    {
      QMutexLocker locker(&mutex);
      if (should_stop) break;
    }
    
    // Capture tmux content
    QString capture_cmd = QString("tmux capture-pane -t %1 -p").arg(session_name);
    QString content = runCommand(capture_cmd);
    
    if (!content.isEmpty()) {
      emit contentReady(content);
    }
    
    // Sleep for refresh interval
    msleep(1000);
  }
}

QString TmuxCaptureThread::runCommand(const QString &command) {
  // Create a temporary file to capture output
  QString temp_file = QString("/tmp/tmux_output_%1.txt").arg((quintptr)QThread::currentThread());
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

#include "tmux_viewer.moc"
// TmuxViewer implementation
TmuxViewer::TmuxViewer(QWidget *parent) 
    : QWidget(parent), connected(false), current_session("default"), 
      is_fullscreen(false), capture_thread(nullptr) {
  setupUI();
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
  
  QString button_style = R"(
    QPushButton {
      border-radius: 25px;
      font-size: 20px;
      font-weight: 500;
      height: 50px;
      padding: 0 25 0 25;
      color: #E4E4E4;
      background-color: #393939;
      min-width: 100px;
    }
    QPushButton:pressed {
      background-color: #4a4a4a;
    }
    QPushButton:disabled {
      color: #33E4E4E4;
      background-color: #2a2a2a;
    }
  )";
  
  connect_btn = new QPushButton("Connect", this);
  connect_btn->setStyleSheet(button_style);
  connect(connect_btn, &QPushButton::clicked, this, &TmuxViewer::toggleConnection);
  
  refresh_btn = new QPushButton("Refresh", this);
  refresh_btn->setStyleSheet(button_style);
  refresh_btn->setEnabled(false);
  connect(refresh_btn, &QPushButton::clicked, this, &TmuxViewer::refreshContent);
  
  fullscreen_btn = new QPushButton("Fullscreen", this);
  fullscreen_btn->setStyleSheet(button_style);
  connect(fullscreen_btn, &QPushButton::clicked, this, &TmuxViewer::toggleFullscreen);
  
  close_btn = new QPushButton("Close", this);
  close_btn->setStyleSheet(button_style);
  close_btn->setVisible(false); // Only show in fullscreen
  connect(close_btn, &QPushButton::clicked, this, &TmuxViewer::toggleFullscreen);
  
  control_layout->addWidget(status_label);
  control_layout->addStretch();
  control_layout->addWidget(close_btn);
  control_layout->addWidget(refresh_btn);
  control_layout->addWidget(fullscreen_btn);
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
      padding: 15px;
      font-family: 'Courier New', monospace;
      font-size: 18px;
      line-height: 1.3;
    }
  )");
  
  // Set monospace font - larger for better readability on rotated device screen
  QFont font("Courier New", 18);
  font.setStyleHint(QFont::TypeWriter);
  font.setWeight(QFont::Medium);
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
  
  // Start background thread for tmux capture
  capture_thread = new TmuxCaptureThread(current_session, this);
  connect(capture_thread, &TmuxCaptureThread::contentReady, 
          this, &TmuxViewer::updateContent, Qt::QueuedConnection);
  capture_thread->start();
}

void TmuxViewer::disconnectFromSession() {
  if (capture_thread) {
    capture_thread->stop();
    capture_thread->wait(3000); // Wait up to 3 seconds for thread to finish
    capture_thread->deleteLater();
    capture_thread = nullptr;
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
    
    // Keep screen active while connected
    setAttribute(Qt::WA_AlwaysShowToolTips, true);
  } else {
    status_label->setText("Disconnected");
    status_label->setStyleSheet("font-size: 24px; font-weight: bold; color: #E4E4E4;");
    connect_btn->setText("Connect");
    refresh_btn->setEnabled(false);
    terminal_display->clear();
    
    // Allow screen to sleep when disconnected
    setAttribute(Qt::WA_AlwaysShowToolTips, false);
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
  // Manual refresh - the background thread handles automatic updates
  if (connected && capture_thread) {
    // Force an immediate update by briefly stopping and restarting thread
    capture_thread->stop();
    capture_thread->wait(1000);
    capture_thread->deleteLater();
    
    capture_thread = new TmuxCaptureThread(current_session, this);
    connect(capture_thread, &TmuxCaptureThread::contentReady, 
            this, &TmuxViewer::updateContent, Qt::QueuedConnection);
    capture_thread->start();
  }
}

void TmuxViewer::updateContent(const QString &content) {
  if (!connected || content.isEmpty()) {
    return;
  }
  
  // Store current scroll position
  QScrollBar *scrollBar = terminal_display->verticalScrollBar();
  bool wasAtBottom = (scrollBar->value() == scrollBar->maximum());
  
  // Update content
  terminal_display->setPlainText(content);
  
  // Restore scroll position or scroll to bottom if we were there
  if (wasAtBottom) {
    scrollBar->setValue(scrollBar->maximum());
  }
}

void TmuxViewer::toggleFullscreen() {
  if (is_fullscreen) {
    // Exit fullscreen
    showNormal();
    fullscreen_btn->setText("Fullscreen");
    fullscreen_btn->setVisible(true);
    close_btn->setVisible(false);
    is_fullscreen = false;
    
    // Reset any transformations
    setTransform(QTransform());
  } else {
    // Enter fullscreen
    showFullScreen();
    fullscreen_btn->setText("Exit Fullscreen");
    fullscreen_btn->setVisible(false);
    close_btn->setVisible(true);
    close_btn->setText("Exit Fullscreen");
    is_fullscreen = true;
    
#ifdef QCOM2
    // Apply proper orientation for comma device
    if (!Hardware::PC()) {
      QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
      if (native && windowHandle()) {
        wl_surface *s = reinterpret_cast<wl_surface*>(
            native->nativeResourceForWindow("surface", windowHandle()));
        if (s) {
          wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
          wl_surface_commit(s);
        }
      }
    }
#endif
  }
  
  // Keep screen awake while in fullscreen
  if (is_fullscreen) {
    // Prevent screen timeout by keeping UI active
    setAttribute(Qt::WA_AlwaysShowToolTips, true);
  } else {
    setAttribute(Qt::WA_AlwaysShowToolTips, false);
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
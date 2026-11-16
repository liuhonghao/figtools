// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Client for the long-lived Python inference worker (python/server.py).
//
// Keeping one process alive avoids paying interpreter start-up on every click.
// The worker keeps loaded models cached, so repeat inference drops from ~1-3 s
// to tens of milliseconds. A failed request never kills the worker; a dead
// worker is restarted transparently on the next call.

#ifndef FIGTOOLS_GUI_INFERENCE_CLIENT_HPP_
#define FIGTOOLS_GUI_INFERENCE_CLIENT_HPP_

#include <QJsonObject>
#include <QString>

#include <memory>

class QProcess;

namespace figtools::gui {

class InferenceClient {
 public:
  InferenceClient();
  ~InferenceClient();

  // Sends one request and waits for its response. `command` is one of the
  // server.py commands: classify / detect / yolo / ocr / segment / ping.
  // Returns an empty object on failure; `error` receives a human message.
  [[nodiscard]] QJsonObject request(const QString& command, const QJsonObject& fields,
                                    QString* error = nullptr, int timeout_ms = 30000);

  // True when a live worker process exists.
  [[nodiscard]] bool alive() const;

  // Terminates the worker (called on app shutdown).
  void shutdown();

 private:
  bool ensureStarted(QString* error);
  void restart();

  std::unique_ptr<QProcess> process_;
  QString python_;
  QString scripts_;
  int next_id_ = 1;
  bool start_failed_ = false;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_INFERENCE_CLIENT_HPP_

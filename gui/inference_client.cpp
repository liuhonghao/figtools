// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "inference_client.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QStringList>

#include "python_env.hpp"

namespace figtools::gui {

InferenceClient::InferenceClient()
    : python_(QSettings().value(QStringLiteral("python/executable"), resolve_python()).toString()),
      scripts_(python_scripts_dir()) {}

InferenceClient::~InferenceClient() { shutdown(); }

bool InferenceClient::alive() const {
  return process_ != nullptr && process_->state() == QProcess::Running;
}

void InferenceClient::shutdown() {
  if (process_ == nullptr) {
    return;
  }
  if (alive()) {
    process_->terminate();
    process_->waitForFinished(2000);
    if (alive()) {
      process_->kill();
      process_->waitForFinished(1000);
    }
  }
  process_.reset();
}

void InferenceClient::restart() {
  shutdown();
  start_failed_ = false;
}

bool InferenceClient::ensureStarted(QString* error) {
  if (alive()) {
    return true;
  }
  if (start_failed_) {
    if (error != nullptr) {
      *error = QStringLiteral("推理服务启动失败，请检查 Python 解释器设置");
    }
    return false;
  }
  if (python_.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("没有找到带 torch 的 Python 解释器");
    }
    return false;
  }

  restart();
  process_ = std::make_unique<QProcess>();
  process_->setWorkingDirectory(scripts_);
  process_->setProcessChannelMode(QProcess::SeparateChannels);
  process_->start(python_, {QStringLiteral("server.py")});
  if (!process_->waitForStarted(10000)) {
    if (error != nullptr) {
      *error = process_->errorString();
    }
    start_failed_ = true;
    restart();
    return false;
  }
  return true;
}

QJsonObject InferenceClient::request(const QString& command, const QJsonObject& fields,
                                     QString* error, int timeout_ms) {
  if (!ensureStarted(error)) {
    return {};
  }

  QJsonObject payload = fields;
  payload.insert(QStringLiteral("id"), next_id_++);
  payload.insert(QStringLiteral("cmd"), command);
  const QByteArray line = QJsonDocument(payload).toJson(QJsonDocument::Compact) + "\n";

  process_->write(line);
  if (!process_->waitForBytesWritten(timeout_ms)) {
    restart();
    if (error != nullptr) {
      *error = QStringLiteral("写入推理服务超时");
    }
    return {};
  }

  QByteArray buffer;
  while (!buffer.contains('\n')) {
    if (!process_->waitForReadyRead(timeout_ms)) {
      restart();
      if (error != nullptr) {
        *error = process_->exitStatus() == QProcess::CrashExit
                     ? QStringLiteral("推理进程异常退出")
                     : QStringLiteral("等待推理结果超时");
      }
      return {};
    }
    buffer += process_->readAllStandardOutput();
    if (process_->state() != QProcess::Running && !buffer.contains('\n')) {
      restart();
      if (error != nullptr) {
        *error = QStringLiteral("推理进程已退出");
      }
      return {};
    }
  }

  const QByteArray response_line = buffer.left(static_cast<qsizetype>(buffer.indexOf('\n')));
  QJsonParseError parse_error{};
  const QJsonDocument document = QJsonDocument::fromJson(response_line, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    if (error != nullptr) {
      *error = QStringLiteral("推理服务返回了无效响应");
    }
    return {};
  }
  const QJsonObject response = document.object();
  if (!response.value(QStringLiteral("ok")).toBool(false)) {
    if (error != nullptr) {
      *error = response.value(QStringLiteral("error")).toString(QStringLiteral("推理失败"));
    }
    return {};
  }
  return response.value(QStringLiteral("result")).toObject();
}

}  // namespace figtools::gui

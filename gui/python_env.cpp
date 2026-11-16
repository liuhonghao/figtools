// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "python_env.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace figtools::gui {
namespace {

bool has_torch(const QDir& env_root) {
  return QFileInfo::exists(env_root.filePath(QStringLiteral("Lib/site-packages/torch"))) ||
         QFileInfo::exists(env_root.filePath(QStringLiteral("lib/python3.10/site-packages/torch")));
}

QString python_in(const QDir& root) {
  const QString windows = root.filePath(QStringLiteral("python.exe"));
  if (QFileInfo::exists(windows)) {
    return windows;
  }
  const QString unix = root.filePath(QStringLiteral("bin/python"));
  return QFileInfo::exists(unix) ? unix : QString();
}

}  // namespace

QString resolve_python() {
  QSettings settings;
  const QString configured = settings.value(QStringLiteral("python/executable")).toString();
  if (!configured.isEmpty() && QFileInfo::exists(configured)) {
    return configured;
  }

  const QString from_env = qEnvironmentVariable("FIGTOOLS_PYTHON");
  if (!from_env.isEmpty() && QFileInfo::exists(from_env)) {
    return from_env;
  }

  // Conda keeps environments in a few predictable places. Prefer ones that
  // actually contain torch: an interpreter without it cannot train anything.
  QStringList roots;
  const QDir home = QDir::home();
  roots << home.filePath(QStringLiteral(".conda/envs"));
  roots << home.filePath(QStringLiteral("anaconda3/envs"));
  roots << home.filePath(QStringLiteral("miniconda3/envs"));
  roots << home.filePath(QStringLiteral("miniforge3/envs"));

  for (const QString& root : roots) {
    QDir directory(root);
    if (!directory.exists()) {
      continue;
    }
    const QStringList entries =
        directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& entry : entries) {
      const QDir env(directory.filePath(entry));
      if (!has_torch(env)) {
        continue;
      }
      const QString candidate = python_in(env);
      if (!candidate.isEmpty()) {
        return candidate;
      }
    }
  }

  for (const QString& base : {QStringLiteral("anaconda3"), QStringLiteral("miniconda3"),
                              QStringLiteral("miniforge3")}) {
    const QDir env(home.filePath(base));
    if (env.exists() && has_torch(env)) {
      const QString candidate = python_in(env);
      if (!candidate.isEmpty()) {
        return candidate;
      }
    }
  }

  const QString on_path = QStandardPaths::findExecutable(QStringLiteral("python"));
  return on_path.isEmpty() ? QStringLiteral("python") : on_path;
}

QString python_scripts_dir() {
  const QString app = QCoreApplication::applicationDirPath();
  const QStringList candidates{
      app + QStringLiteral("/python"),
      app + QStringLiteral("/../../python"),
      QDir::currentPath() + QStringLiteral("/python"),
  };
  for (const QString& candidate : candidates) {
    if (QFileInfo::exists(candidate + QStringLiteral("/train.py"))) {
      return QDir(candidate).absolutePath();
    }
  }
  return app + QStringLiteral("/python");
}

QString python_hint(const QString& executable) {
  const QFileInfo info(executable);
  return info.exists() ? info.absoluteFilePath() : executable;
}

}  // namespace figtools::gui

/*
    SPDX-FileCopyrightText: 2026 Kdenlive AquesTalk fork contributors

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "aquestalkwidget.hpp"

#include "bin/bin.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

AquesTalkWidget::AquesTalkWidget(QWidget *parent)
    : QWidget(parent)
    , m_network(new QNetworkAccessManager(this))
{
    auto *mainLayout = new QVBoxLayout(this);

    auto *form = new QFormLayout;
    m_serverEdit = new QLineEdit(KdenliveSettings::aquestalk_server(), this);
    form->addRow(i18n("Server:"), m_serverEdit);

    m_voiceCombo = new QComboBox(this);
    // Voice ids supported by aquestalk-server (AquesTalk1, old license)
    const QStringList voices = {QStringLiteral("f1"), QStringLiteral("f2"), QStringLiteral("m1"), QStringLiteral("m2"),
                                QStringLiteral("dvd"), QStringLiteral("imd1"), QStringLiteral("jgr"), QStringLiteral("r1")};
    m_voiceCombo->addItems(voices);
    m_voiceCombo->setCurrentText(KdenliveSettings::aquestalk_voice());
    form->addRow(i18n("Voice:"), m_voiceCombo);

    m_speedSpin = new QDoubleSpinBox(this);
    m_speedSpin->setRange(0.5, 3.0);
    m_speedSpin->setSingleStep(0.05);
    m_speedSpin->setValue(KdenliveSettings::aquestalk_speed());
    form->addRow(i18n("Speed:"), m_speedSpin);
    mainLayout->addLayout(form);

    m_placeCheck = new QCheckBox(i18n("Place on timeline"), this);
    m_placeCheck->setChecked(true);
    mainLayout->addWidget(m_placeCheck);
    m_subtitleCheck = new QCheckBox(i18n("Write subtitles (.srt)"), this);
    m_subtitleCheck->setChecked(true);
    mainLayout->addWidget(m_subtitleCheck);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setPlaceholderText(i18n("One line = one utterance"));
    mainLayout->addWidget(m_textEdit, 1);

    auto *buttonRow = new QHBoxLayout;
    m_generateButton = new QPushButton(i18n("Generate and Add to Bin"), this);
    buttonRow->addWidget(m_generateButton, 1);
    m_cancelButton = new QPushButton(i18n("Cancel"), this);
    m_cancelButton->setVisible(false);
    buttonRow->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonRow);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    mainLayout->addWidget(m_progress);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    mainLayout->addWidget(m_status);

    connect(m_generateButton, &QPushButton::clicked, this, &AquesTalkWidget::slotGenerate);
    connect(m_cancelButton, &QPushButton::clicked, this, &AquesTalkWidget::slotCancel);
    connect(m_network, &QNetworkAccessManager::finished, this, &AquesTalkWidget::slotReplyFinished);
}

AquesTalkWidget::~AquesTalkWidget() = default;

QString AquesTalkWidget::outputDirectory() const
{
    QString base;
    if (KdenliveDoc *doc = pCore->currentDoc()) {
        base = doc->folderForProjectFiles();
    }
    if (base.isEmpty()) {
        // Project not saved yet (or storage type has no folder): fall back to a temp folder
        base = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/kdenlive-aquestalk");
    }
    const QString dir = base + QStringLiteral("/aquestalk");
    QDir().mkpath(dir);
    return dir;
}

QString AquesTalkWidget::uniqueFilePath(const QString &baseName, const QString &extension) const
{
    // Never overwrite an existing file: clips already in the bin/timeline
    // reference WAVs by path, so overwriting would silently change their audio.
    const QString dir = outputDirectory();
    QString path = QStringLiteral("%1/%2.%3").arg(dir, baseName, extension);
    int suffix = 2;
    while (QFile::exists(path)) {
        path = QStringLiteral("%1/%2_%3.%4").arg(dir, baseName).arg(suffix++).arg(extension);
    }
    return path;
}

double AquesTalkWidget::wavDurationSeconds(const QByteArray &wav)
{
    // Minimal RIFF parse: byte rate at offset 28, "data" chunk size after it.
    if (wav.size() < 44 || !wav.startsWith("RIFF")) {
        return 0.0;
    }
    auto u32le = [&wav](int offset) -> quint32 {
        return quint32(quint8(wav[offset])) | (quint32(quint8(wav[offset + 1])) << 8) | (quint32(quint8(wav[offset + 2])) << 16) |
               (quint32(quint8(wav[offset + 3])) << 24);
    };
    const quint32 byteRate = u32le(28);
    if (byteRate == 0) {
        return 0.0;
    }
    int offset = 12;
    while (offset + 8 <= wav.size()) {
        const quint32 chunkSize = u32le(offset + 4);
        if (wav.mid(offset, 4) == "data") {
            return double(chunkSize) / double(byteRate);
        }
        offset += 8 + int(chunkSize) + (chunkSize % 2);
    }
    return 0.0;
}

void AquesTalkWidget::slotGenerate()
{
    if (m_running) {
        return;
    }
    m_queue.clear();
    const QStringList lines = m_textEdit->toPlainText().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            m_queue.append(trimmed);
        }
    }
    if (m_queue.isEmpty()) {
        m_status->setText(i18n("No text"));
        return;
    }
    // Persist settings for next time
    KdenliveSettings::setAquestalk_server(m_serverEdit->text().trimmed());
    KdenliveSettings::setAquestalk_voice(m_voiceCombo->currentText());
    KdenliveSettings::setAquestalk_speed(m_speedSpin->value());

    m_total = m_queue.size();
    m_done = 0;
    m_failed = 0;
    m_cancelled = false;
    m_generatedBinIds.clear();
    m_generatedTexts.clear();
    m_generatedDurations.clear();
    // Batch stamp makes file names unique across runs (no overwriting)
    m_batchStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    m_running = true;
    m_generateButton->setEnabled(false);
    m_cancelButton->setVisible(true);
    m_progress->setRange(0, m_total);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    sendNext();
}

void AquesTalkWidget::slotCancel()
{
    if (!m_running) {
        return;
    }
    m_cancelled = true;
    if (m_currentReply) {
        m_currentReply->abort();
    }
}

void AquesTalkWidget::sendNext()
{
    if (m_cancelled) {
        finishQueue(i18n("Cancelled: %1 clip(s) generated", m_done));
        return;
    }
    if (m_queue.isEmpty()) {
        QString message = i18n("Done: %1 clip(s) added to the bin", m_done);
        if (m_placeCheck->isChecked() && !m_generatedBinIds.isEmpty()) {
            const QString placeError = placeOnTimeline();
            if (!placeError.isEmpty()) {
                message += QStringLiteral(" / ") + placeError;
            }
        }
        if (m_subtitleCheck->isChecked() && !m_generatedTexts.isEmpty()) {
            writeSubtitles();
        }
        if (m_failed > 0) {
            message += i18n(" (%1 failed)", m_failed);
        }
        finishQueue(message);
        return;
    }
    const QString text = m_queue.first();
    m_status->setText(i18n("Generating %1/%2: %3", m_done + m_failed + 1, m_total, text.left(30)));

    QString server = m_serverEdit->text().trimmed();
    if (server.endsWith(QLatin1Char('/'))) {
        server.chop(1);
    }
    QNetworkRequest request{QUrl(server + QStringLiteral("/v1/audio/speech"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject body;
    body.insert(QStringLiteral("model"), QStringLiteral("tts-1"));
    body.insert(QStringLiteral("input"), text);
    body.insert(QStringLiteral("voice"), m_voiceCombo->currentText());
    body.insert(QStringLiteral("response_format"), QStringLiteral("wav"));
    body.insert(QStringLiteral("speed"), m_speedSpin->value());
    m_currentReply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void AquesTalkWidget::slotReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply == m_currentReply) {
        m_currentReply = nullptr;
    }
    if (!m_running) {
        return;
    }
    if (m_cancelled) {
        finishQueue(i18n("Cancelled: %1 clip(s) generated", m_done));
        return;
    }
    const QString text = m_queue.takeFirst();
    if (reply->error() != QNetworkReply::NoError) {
        // Network-level failure: the server is most likely down, abort the queue
        finishQueue(i18n("Failed: %1 (is aquestalk-server running at %2?)", reply->errorString(), m_serverEdit->text().trimmed()));
        return;
    }
    const QByteArray wav = reply->readAll();
    if (wav.size() < 44 || !wav.startsWith("RIFF")) {
        // Bad payload for this line only: skip it and continue with the rest
        m_failed++;
        m_progress->setValue(m_done + m_failed);
        sendNext();
        return;
    }
    const QString baseName = QStringLiteral("%1_%2_%3").arg(m_batchStamp).arg(m_done + m_failed + 1, 3, 10, QLatin1Char('0')).arg(m_voiceCombo->currentText());
    const QString fileName = uniqueFilePath(baseName);
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size()) {
        m_failed++;
        m_progress->setValue(m_done + m_failed);
        sendNext();
        return;
    }
    file.close();
    // Import into the project bin; keep the bin id for timeline placement
    const QString binId = pCore->activeBin()->slotAddClipToProject(QUrl::fromLocalFile(fileName));
    if (binId.isEmpty()) {
        m_failed++;
    } else {
        m_done++;
        m_generatedBinIds.append(binId);
        m_generatedTexts.append(text);
        m_generatedDurations.append(wavDurationSeconds(wav));
    }
    m_progress->setValue(m_done + m_failed);
    sendNext();
}

QString AquesTalkWidget::placeOnTimeline()
{
    TimelineWidget *timeline = pCore->window() ? pCore->window()->getCurrentTimeline() : nullptr;
    if (!timeline || !timeline->controller() || !timeline->model()) {
        return i18n("No timeline available for placement");
    }
    const QList<int> audioTracks = timeline->model()->getTracksIds(true);
    if (audioTracks.isEmpty()) {
        return i18n("No audio track for placement");
    }
    const int position = pCore->getMonitorPosition();
    const QList<int> inserted = timeline->controller()->insertClips(audioTracks.first(), position, m_generatedBinIds, true, true);
    if (inserted.isEmpty()) {
        return i18n("Timeline placement failed (track busy at playhead?)");
    }
    return QString();
}

void AquesTalkWidget::writeSubtitles()
{
    auto formatTime = [](double seconds) {
        const int totalMs = int(seconds * 1000.0);
        const int ms = totalMs % 1000;
        const int s = (totalMs / 1000) % 60;
        const int m = (totalMs / 60000) % 60;
        const int h = totalMs / 3600000;
        return QStringLiteral("%1:%2:%3,%4")
            .arg(h, 2, 10, QLatin1Char('0'))
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'))
            .arg(ms, 3, 10, QLatin1Char('0'));
    };
    QString srt;
    double cursor = 0.0;
    for (int i = 0; i < m_generatedTexts.size(); ++i) {
        const double duration = (i < m_generatedDurations.size() && m_generatedDurations.at(i) > 0.0) ? m_generatedDurations.at(i) : 1.0;
        srt += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(i + 1).arg(formatTime(cursor), formatTime(cursor + duration), m_generatedTexts.at(i));
        cursor += duration;
    }
    const QString path = uniqueFilePath(m_batchStamp + QStringLiteral("_subtitles"), QStringLiteral("srt"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(srt.toUtf8());
        file.close();
    }
}

void AquesTalkWidget::finishQueue(const QString &message)
{
    m_running = false;
    m_queue.clear();
    m_currentReply = nullptr;
    m_generateButton->setEnabled(true);
    m_cancelButton->setVisible(false);
    m_progress->setVisible(false);
    m_status->setText(message);
    pCore->displayMessage(message, m_failed == 0 && !m_cancelled ? MessageType::InformationMessage : MessageType::ErrorMessage, 500);
}

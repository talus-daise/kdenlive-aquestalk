/*
    SPDX-FileCopyrightText: 2026 Kdenlive AquesTalk fork contributors

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "aquestalkwidget.hpp"

#include "bin/bin.h"
#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"

#include <KLocalizedString>

#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

namespace {
const QStringList &voiceIds()
{
    static const QStringList voices = {QStringLiteral("f1"), QStringLiteral("f2"), QStringLiteral("m1"), QStringLiteral("m2"),
                                       QStringLiteral("dvd"), QStringLiteral("imd1"), QStringLiteral("jgr"), QStringLiteral("r1")};
    return voices;
}
} // namespace

AquesTalkWidget::AquesTalkWidget(QWidget *parent)
    : QWidget(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_player(new QMediaPlayer(this))
    , m_audioOutput(new QAudioOutput(this))
{
    m_player->setAudioOutput(m_audioOutput);
    auto *mainLayout = new QVBoxLayout(this);

    auto *form = new QFormLayout;
    m_serverEdit = new QLineEdit(KdenliveSettings::aquestalk_server(), this);
    form->addRow(i18n("Server:"), m_serverEdit);

    m_voiceCombo = new QComboBox(this);
    m_voiceCombo->addItems(voiceIds());
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
    m_subtitleCheck = new QCheckBox(i18n("Add subtitles"), this);
    m_subtitleCheck->setChecked(true);
    mainLayout->addWidget(m_subtitleCheck);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setPlaceholderText(i18n("One line = one utterance"));
    mainLayout->addWidget(m_textEdit, 1);

    m_presetsButton = new QPushButton(i18n("Characters / Dictionary…"), this);
    mainLayout->addWidget(m_presetsButton);

    auto *buttonRow = new QHBoxLayout;
    m_generateButton = new QPushButton(i18n("Generate All"), this);
    buttonRow->addWidget(m_generateButton, 1);
    m_lineButton = new QPushButton(i18n("Generate Line"), this);
    buttonRow->addWidget(m_lineButton);
    m_previewButton = new QPushButton(i18n("Preview Line"), this);
    buttonRow->addWidget(m_previewButton);
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
    connect(m_lineButton, &QPushButton::clicked, this, &AquesTalkWidget::slotGenerateLine);
    connect(m_previewButton, &QPushButton::clicked, this, &AquesTalkWidget::slotPreviewLine);
    connect(m_presetsButton, &QPushButton::clicked, this, &AquesTalkWidget::slotEditPresets);
    connect(m_cancelButton, &QPushButton::clicked, this, &AquesTalkWidget::slotCancel);
    connect(m_network, &QNetworkAccessManager::finished, this, &AquesTalkWidget::slotReplyFinished);
}

AquesTalkWidget::~AquesTalkWidget() = default;

QMap<QString, AquesTalkWidget::Preset> AquesTalkWidget::presets() const
{
    QMap<QString, Preset> result;
    const QStringList lines = KdenliveSettings::aquestalk_presets().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        // Format: name:voice:speed:image — speed and image are optional.
        // The image path may itself contain ':' (Windows paths), so only
        // the first three fields are split off and the rest is the path.
        const QStringList parts = line.split(QLatin1Char(':'));
        if (parts.size() < 2) {
            continue;
        }
        const QString name = parts.at(0).trimmed();
        const QString voice = parts.at(1).trimmed();
        if (name.isEmpty() || !voiceIds().contains(voice)) {
            continue;
        }
        Preset preset;
        preset.voice = voice;
        preset.speed = m_speedSpin ? m_speedSpin->value() : 1.0;
        if (parts.size() >= 3 && !parts.at(2).trimmed().isEmpty()) {
            bool ok = false;
            const double parsed = parts.at(2).trimmed().toDouble(&ok);
            if (ok && parsed >= 0.5 && parsed <= 3.0) {
                preset.speed = parsed;
            }
        }
        if (parts.size() >= 4) {
            preset.image = parts.mid(3).join(QLatin1Char(':')).trimmed();
        }
        result.insert(name, preset);
    }
    return result;
}

QList<QPair<QString, QString>> AquesTalkWidget::dictionary() const
{
    QList<QPair<QString, QString>> result;
    const QStringList lines = KdenliveSettings::aquestalk_dictionary().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int sep = line.indexOf(QLatin1Char('='));
        if (sep <= 0) {
            continue;
        }
        const QString before = line.left(sep).trimmed();
        const QString after = line.mid(sep + 1).trimmed();
        if (!before.isEmpty()) {
            result.append({before, after});
        }
    }
    return result;
}

AquesTalkWidget::Line AquesTalkWidget::parseLine(const QString &raw) const
{
    Line line;
    line.voice = m_voiceCombo->currentText();
    line.speed = m_speedSpin->value();
    QString text = raw.trimmed();

    // Character preset: "name> text"
    static const QRegularExpression presetRe(QStringLiteral("^(.{1,24}?)\\s*>\\s*(.+)$"));
    const QRegularExpressionMatch presetMatch = presetRe.match(text);
    if (presetMatch.hasMatch()) {
        const auto allPresets = presets();
        const QString name = presetMatch.captured(1).trimmed();
        if (allPresets.contains(name)) {
            const Preset preset = allPresets.value(name);
            line.voice = preset.voice;
            line.speed = preset.speed;
            line.character = name;
            line.imagePath = preset.image;
            text = presetMatch.captured(2).trimmed();
        }
    }
    // Inline override: "[f2] text" or "[f2@1.2] text"
    static const QRegularExpression inlineRe(QStringLiteral("^\\[(f1|f2|m1|m2|dvd|imd1|jgr|r1)(?:@([0-9]*\\.?[0-9]+))?\\]\\s*(.+)$"));
    const QRegularExpressionMatch inlineMatch = inlineRe.match(text);
    if (inlineMatch.hasMatch()) {
        line.voice = inlineMatch.captured(1);
        if (!inlineMatch.captured(2).isEmpty()) {
            const double speed = inlineMatch.captured(2).toDouble();
            if (speed >= 0.5 && speed <= 3.0) {
                line.speed = speed;
            }
        }
        text = inlineMatch.captured(3).trimmed();
    }

    // The spoken text keeps the full AquesTalk phonetic notation;
    // the subtitle/display text is derived from it (see buildDisplayText).
    line.displayText = buildDisplayText(text);
    // Reading-correction dictionary applies to the spoken text only
    QString spoken = text;
    const auto dict = dictionary();
    for (const auto &entry : dict) {
        spoken.replace(entry.first, entry.second);
    }
    line.spokenText = spoken;
    return line;
}

QList<AquesTalkWidget::Line> AquesTalkWidget::parseLines(const QStringList &rawLines) const
{
    QList<Line> result;
    for (const QString &raw : rawLines) {
        if (!raw.trimmed().isEmpty()) {
            result.append(parseLine(raw));
        }
    }
    return result;
}

QString AquesTalkWidget::currentLineText() const
{
    return m_textEdit->textCursor().block().text();
}

void AquesTalkWidget::slotEditPresets()
{
    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Characters / Dictionary"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(i18n("Characters (name:voice:speed:image)"), &dialog));
    auto *presetsEdit = new QPlainTextEdit(KdenliveSettings::aquestalk_presets(), &dialog);
    layout->addWidget(presetsEdit);
    layout->addWidget(new QLabel(i18n("Dictionary (before=after)"), &dialog));
    auto *dictEdit = new QPlainTextEdit(KdenliveSettings::aquestalk_dictionary(), &dialog);
    layout->addWidget(dictEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        KdenliveSettings::setAquestalk_presets(presetsEdit->toPlainText());
        KdenliveSettings::setAquestalk_dictionary(dictEdit->toPlainText());
    }
}

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

QString AquesTalkWidget::buildDisplayText(const QString &text)
{
    // Derive the subtitle/display text from a line that may contain
    // AquesTalk phonetic notation (see the AquesTalk phonetic-string spec).
    // Rules:
    //  - Lines without any phonetic notation are returned untouched, so a
    //    literal "+", "/" or "," in normal prose is never eaten.
    //  - <NUM>/<NUMK>/<ALPHA> tags are replaced by their VAL contents:
    //    the numbers/letters must stay visible in subtitles.
    //  - Accent marks (') are pure control symbols: removed.
    //  - Phrase separators / ; + between kana are phrase boundaries with
    //    no display meaning: removed (only between kana, so "1+1" or
    //    "C++" in prose-like content is not affected).
    //  - Halfwidth comma is a short pause: shown as 、.
    //  - Devoicing marker _ (prefix of a katakana syllable): removed,
    //    the kana itself is kept.
    //  - Nasal marker ゜ is part of the reading symbol: kept as written.
    QString display = text;
    static const QRegularExpression quotedTagRe(QStringLiteral("<(NUMK?|ALPHA)\\s+VAL=\"([^\"]+)\"(?:\\s+COUNTER=[^>]*)?>"));
    static const QRegularExpression plainTagRe(QStringLiteral("<(NUMK?|ALPHA)\\s+VAL=([^\\s>]+)(?:\\s+COUNTER=[^>]*)?>"));
    static const QRegularExpression separatorRe(QStringLiteral("(?<=[\\x{3040}-\\x{30FF}])[/;+](?=[\\x{3040}-\\x{30FF}])"));
    // Separator right after a number/tag value, e.g. "<NUMK VAL=...>+でした"
    static const QRegularExpression separatorAfterDigitRe(QStringLiteral("(?<=[0-9])[/;+](?=[\\x{3040}-\\x{30FF}])"));
    // Separator right before a number/tag value, e.g. "あと/<NUMK VAL=...>"
    static const QRegularExpression separatorBeforeDigitRe(QStringLiteral("(?<=[\\x{3040}-\\x{30FF}])[/;+](?=[0-9])"));

    bool phonetic = display.contains(QLatin1Char('\'')) || display.contains(quotedTagRe) || display.contains(plainTagRe) || display.contains(separatorRe);
    display.replace(quotedTagRe, QStringLiteral("\\2"));
    display.replace(plainTagRe, QStringLiteral("\\2"));
    if (!phonetic) {
        return display;
    }
    display.remove(QLatin1Char('\''));
    display.replace(QLatin1Char(','), QStringLiteral("、"));
    display.replace(separatorRe, QString());
    display.replace(separatorAfterDigitRe, QString());
    display.replace(separatorBeforeDigitRe, QString());
    // Devoicing marker is a prefix of the devoiced katakana syllable
    static const QRegularExpression devoicingRe(QStringLiteral("_(?=[\\x{30A0}-\\x{30FF}])"));
    display.replace(devoicingRe, QString());
    return display;
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
    startQueue(parseLines(m_textEdit->toPlainText().split(QLatin1Char('\n'))), false);
}

void AquesTalkWidget::slotGenerateLine()
{
    const QString raw = currentLineText();
    if (raw.trimmed().isEmpty()) {
        m_status->setText(i18n("No text"));
        return;
    }
    startQueue({parseLine(raw)}, false);
}

void AquesTalkWidget::slotPreviewLine()
{
    const QString raw = currentLineText();
    if (raw.trimmed().isEmpty()) {
        m_status->setText(i18n("No text"));
        return;
    }
    startQueue({parseLine(raw)}, true);
}

void AquesTalkWidget::startQueue(const QList<Line> &lines, bool previewOnly)
{
    if (m_running) {
        return;
    }
    if (lines.isEmpty()) {
        m_status->setText(i18n("No text"));
        return;
    }
    // Persist settings for next time
    KdenliveSettings::setAquestalk_server(m_serverEdit->text().trimmed());
    KdenliveSettings::setAquestalk_voice(m_voiceCombo->currentText());
    KdenliveSettings::setAquestalk_speed(m_speedSpin->value());

    m_queue = lines;
    m_previewOnly = previewOnly;
    m_total = m_queue.size();
    m_done = 0;
    m_failed = 0;
    m_cancelled = false;
    m_generatedBinIds.clear();
    m_generatedTexts.clear();
    m_generatedImages.clear();
    m_generatedDurations.clear();
    // Batch stamp makes file names unique across runs (no overwriting)
    m_batchStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    m_running = true;
    m_generateButton->setEnabled(false);
    m_lineButton->setEnabled(false);
    m_previewButton->setEnabled(false);
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
        if (m_previewOnly) {
            finishQueue(i18n("Preview done"));
            return;
        }
        QString message = i18n("Done: %1 clip(s) added to the bin", m_done);
        int placedPosition = -1;
        if (m_placeCheck->isChecked() && !m_generatedBinIds.isEmpty()) {
            placedPosition = placeOnTimeline();
            if (placedPosition < 0) {
                message += QStringLiteral(" / ") + i18n("Timeline placement failed (no timeline, no audio track, or track busy at playhead)");
            }
        }
        if (m_subtitleCheck->isChecked() && !m_generatedTexts.isEmpty()) {
            const QString srtPath = writeSubtitles();
            if (!srtPath.isEmpty() && placedPosition >= 0) {
                if (!addSubtitlesToTrack(srtPath, placedPosition)) {
                    message += QStringLiteral(" / ") + i18n("Subtitles saved as .srt file");
                }
            }
        }
        if (m_failed > 0) {
            message += i18n(" (%1 failed)", m_failed);
        }
        finishQueue(message);
        return;
    }
    const Line &line = m_queue.first();
    m_status->setText(i18n("Generating %1/%2: %3", m_done + m_failed + 1, m_total, line.displayText.left(30)));

    QString server = m_serverEdit->text().trimmed();
    if (server.endsWith(QLatin1Char('/'))) {
        server.chop(1);
    }
    QNetworkRequest request{QUrl(server + QStringLiteral("/v1/audio/speech"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject body;
    body.insert(QStringLiteral("model"), QStringLiteral("tts-1"));
    body.insert(QStringLiteral("input"), line.spokenText);
    body.insert(QStringLiteral("voice"), line.voice);
    body.insert(QStringLiteral("response_format"), QStringLiteral("wav"));
    body.insert(QStringLiteral("speed"), line.speed);
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
    const Line line = m_queue.takeFirst();
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
    if (m_previewOnly) {
        const QString fileName = uniqueFilePath(QStringLiteral("preview"));
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly) && file.write(wav) == wav.size()) {
            file.close();
            m_done++;
            m_player->setSource(QUrl::fromLocalFile(fileName));
            m_player->play();
        } else {
            m_failed++;
        }
        m_progress->setValue(m_done + m_failed);
        sendNext();
        return;
    }
    const QString baseName = QStringLiteral("%1_%2_%3").arg(m_batchStamp).arg(m_done + m_failed + 1, 3, 10, QLatin1Char('0')).arg(line.voice);
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
        m_generatedTexts.append(line.displayText);
        m_generatedImages.append(line.imagePath);
        m_generatedDurations.append(wavDurationSeconds(wav));
    }
    m_progress->setValue(m_done + m_failed);
    sendNext();
}

int AquesTalkWidget::placeOnTimeline()
{
    TimelineWidget *timeline = pCore->window() ? pCore->window()->getCurrentTimeline() : nullptr;
    if (!timeline || !timeline->controller() || !timeline->model()) {
        return -1;
    }
    const QList<int> audioTracks = timeline->model()->getTracksIds(true);
    if (audioTracks.isEmpty()) {
        return -1;
    }
    const int position = pCore->getMonitorPosition();
    const QList<int> inserted = timeline->controller()->insertClips(audioTracks.first(), position, m_generatedBinIds, true, true);
    if (inserted.isEmpty()) {
        return -1;
    }
    placeCharacterImages(timeline, position);
    return position;
}

void AquesTalkWidget::placeCharacterImages(TimelineWidget *timeline, int startPosition)
{
    // Standing images (tachie): for each line whose character preset has an
    // image, put the image on the first video track for exactly the span of
    // that line's audio. Per-line segments; no mouth/blink animation yet.
    bool anyImage = false;
    for (const QString &image : m_generatedImages) {
        if (!image.isEmpty()) {
            anyImage = true;
            break;
        }
    }
    if (!anyImage) {
        return;
    }
    const QList<int> videoTracks = timeline->model()->getTracksIds(false);
    if (videoTracks.isEmpty()) {
        return;
    }
    const int videoTrack = videoTracks.first();
    const double fps = pCore->getCurrentFps();
    QMap<QString, QString> binIdByImage;
    int cursor = startPosition;
    for (int i = 0; i < m_generatedBinIds.size(); ++i) {
        const double seconds = (i < m_generatedDurations.size() && m_generatedDurations.at(i) > 0.0) ? m_generatedDurations.at(i) : 1.0;
        const int frames = qMax(1, int(seconds * fps + 0.5));
        const QString image = (i < m_generatedImages.size()) ? m_generatedImages.at(i) : QString();
        if (!image.isEmpty() && QFile::exists(image)) {
            QString imageBinId = binIdByImage.value(image);
            if (imageBinId.isEmpty()) {
                imageBinId = pCore->activeBin()->slotAddClipToProject(QUrl::fromLocalFile(image));
                if (!imageBinId.isEmpty()) {
                    binIdByImage.insert(image, imageBinId);
                }
            }
            if (!imageBinId.isEmpty()) {
                const QList<int> placed = timeline->controller()->insertClips(videoTrack, cursor, {imageBinId}, true, true);
                if (!placed.isEmpty()) {
                    // Stretch/shrink the image clip to the audio length
                    timeline->model()->requestItemResize(placed.first(), frames, true, true);
                }
            }
        }
        cursor += frames;
    }
}

QString AquesTalkWidget::writeSubtitles()
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
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }
    file.write(srt.toUtf8());
    file.close();
    return path;
}

bool AquesTalkWidget::addSubtitlesToTrack(const QString &srtPath, int offsetFrames)
{
    TimelineWidget *timeline = pCore->window() ? pCore->window()->getCurrentTimeline() : nullptr;
    if (!timeline || !timeline->model()) {
        return false;
    }
    auto subtitleModel = timeline->model()->getSubtitleModel();
    if (!subtitleModel) {
        return false;
    }
    // Same pattern as the speech-to-text dialog: import the SRT at the
    // placement position (offset in frames) as an external import.
    subtitleModel->importSubtitle(srtPath, offsetFrames, true);
    return true;
}

void AquesTalkWidget::finishQueue(const QString &message)
{
    m_running = false;
    m_queue.clear();
    m_currentReply = nullptr;
    m_generateButton->setEnabled(true);
    m_lineButton->setEnabled(true);
    m_previewButton->setEnabled(true);
    m_cancelButton->setVisible(false);
    m_progress->setVisible(false);
    m_status->setText(message);
    pCore->displayMessage(message, m_failed == 0 && !m_cancelled ? MessageType::InformationMessage : MessageType::ErrorMessage, 500);
}

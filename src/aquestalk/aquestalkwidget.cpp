/*
    SPDX-FileCopyrightText: 2026 Kdenlive AquesTalk fork contributors

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "aquestalkwidget.hpp"

#include "bin/bin.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QDoubleSpinBox>
#include <QFormLayout>
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

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setPlaceholderText(i18n("One line = one utterance"));
    mainLayout->addWidget(m_textEdit, 1);

    m_generateButton = new QPushButton(i18n("Generate and Add to Bin"), this);
    mainLayout->addWidget(m_generateButton);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    mainLayout->addWidget(m_progress);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    mainLayout->addWidget(m_status);

    connect(m_generateButton, &QPushButton::clicked, this, &AquesTalkWidget::slotGenerate);
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
    m_running = true;
    m_generateButton->setEnabled(false);
    m_progress->setRange(0, m_total);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    sendNext();
}

void AquesTalkWidget::sendNext()
{
    if (m_queue.isEmpty()) {
        finishQueue(true, i18n("Done: %1 clip(s) added to the bin", m_done));
        return;
    }
    const QString text = m_queue.first();
    m_status->setText(i18n("Generating %1/%2: %3", m_done + 1, m_total, text.left(30)));

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
    m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void AquesTalkWidget::slotReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (!m_running) {
        return;
    }
    const QString text = m_queue.takeFirst();
    if (reply->error() != QNetworkReply::NoError) {
        finishQueue(false, i18n("Failed: %1 (is aquestalk-server running at %2?)", reply->errorString(), m_serverEdit->text().trimmed()));
        return;
    }
    const QByteArray wav = reply->readAll();
    if (wav.size() < 44 || !wav.startsWith("RIFF")) {
        finishQueue(false, i18n("Failed: server did not return a WAV file"));
        return;
    }
    const QString fileName = QStringLiteral("%1/%2_%3.wav")
                                 .arg(outputDirectory())
                                 .arg(m_done + 1, 3, 10, QLatin1Char('0'))
                                 .arg(m_voiceCombo->currentText());
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size()) {
        finishQueue(false, i18n("Failed: could not write %1", fileName));
        return;
    }
    file.close();
    // Import into the project bin; from there it can be dropped on the timeline
    pCore->activeBin()->slotAddClipToProject(QUrl::fromLocalFile(fileName));
    m_done++;
    m_progress->setValue(m_done);
    sendNext();
}

void AquesTalkWidget::finishQueue(bool success, const QString &message)
{
    m_running = false;
    m_queue.clear();
    m_generateButton->setEnabled(true);
    m_progress->setVisible(false);
    m_status->setText(message);
    pCore->displayMessage(message, success ? MessageType::InformationMessage : MessageType::ErrorMessage, 500);
}

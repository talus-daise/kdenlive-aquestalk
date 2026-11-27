/*
    SPDX-FileCopyrightText: 2026 Kdenlive AquesTalk fork contributors

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

/** @class AquesTalkWidget
    @brief Dock widget that generates speech with an aquestalk-server
           (https://github.com/Lqm1/aquestalk-server, OpenAI compatible API)
           and adds the resulting WAV files to the project bin.

    One line of text = one utterance, generated sequentially.
    Optionally places the generated clips on the timeline (first audio
    track, at the playhead) and writes an SRT subtitle file.
    The server itself is external: this widget only speaks HTTP to it.
*/
class AquesTalkWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AquesTalkWidget(QWidget *parent = nullptr);
    ~AquesTalkWidget() override;

private Q_SLOTS:
    void slotGenerate();
    void slotCancel();
    void slotReplyFinished(QNetworkReply *reply);

private:
    void sendNext();
    void finishQueue(const QString &message);
    QString placeOnTimeline();
    void writeSubtitles();
    QString outputDirectory() const;
    QString uniqueFilePath(const QString &baseName, const QString &extension = QStringLiteral("wav")) const;
    static double wavDurationSeconds(const QByteArray &wav);

    QLineEdit *m_serverEdit{nullptr};
    QComboBox *m_voiceCombo{nullptr};
    QDoubleSpinBox *m_speedSpin{nullptr};
    QCheckBox *m_placeCheck{nullptr};
    QCheckBox *m_subtitleCheck{nullptr};
    QPlainTextEdit *m_textEdit{nullptr};
    QPushButton *m_generateButton{nullptr};
    QPushButton *m_cancelButton{nullptr};
    QProgressBar *m_progress{nullptr};
    QLabel *m_status{nullptr};

    QNetworkAccessManager *m_network{nullptr};
    QNetworkReply *m_currentReply{nullptr};
    QStringList m_queue;
    QStringList m_generatedBinIds;
    QStringList m_generatedTexts;
    QList<double> m_generatedDurations;
    QString m_batchStamp;
    int m_total{0};
    int m_done{0};
    int m_failed{0};
    bool m_running{false};
    bool m_cancelled{false};
};

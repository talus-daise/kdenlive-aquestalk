/*
    SPDX-FileCopyrightText: 2026 Kdenlive AquesTalk fork contributors

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QList>
#include <QMap>
#include <QStringList>
#include <QWidget>

class QAudioOutput;
class TimelineWidget;

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QMediaPlayer;
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
    Per-line overrides:
      - Character preset:  "name> text"   (presets: name:voice:speed)
      - Inline override:   "[f2@1.2] text" or "[f2] text"
    A reading-correction dictionary (before=after lines) is applied to the
    spoken text only; subtitles keep the original text.
    Optionally places the generated clips on the timeline (first audio
    track, at the playhead) and adds subtitles to the timeline's subtitle
    track (falling back to an .srt file next to the WAVs).
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
    void slotGenerateLine();
    void slotPreviewLine();
    void slotCancel();
    void slotEditPresets();
    void slotReplyFinished(QNetworkReply *reply);

private:
    struct Line {
        QString spokenText;
        QString displayText;
        QString voice;
        double speed{1.0};
        QString character;
        QString imagePath;
    };
    struct Preset {
        QString voice;
        double speed{1.0};
        QString image;
    };

    void startQueue(const QList<Line> &lines, bool previewOnly);
    QList<Line> parseLines(const QStringList &rawLines) const;
    Line parseLine(const QString &raw) const;
    QMap<QString, Preset> presets() const;
    QList<QPair<QString, QString>> dictionary() const;
    QString currentLineText() const;
    void sendNext();
    void finishQueue(const QString &message);
    int placeOnTimeline();
    void placeCharacterImages(TimelineWidget *timeline, int startPosition);
    QString writeSubtitles();
    bool addSubtitlesToTrack(const QString &srtPath, int offsetFrames);
    QString outputDirectory() const;
    QString uniqueFilePath(const QString &baseName, const QString &extension = QStringLiteral("wav")) const;
    static double wavDurationSeconds(const QByteArray &wav);
    static QString buildDisplayText(const QString &text);

    QLineEdit *m_serverEdit{nullptr};
    QComboBox *m_voiceCombo{nullptr};
    QDoubleSpinBox *m_speedSpin{nullptr};
    QCheckBox *m_placeCheck{nullptr};
    QCheckBox *m_subtitleCheck{nullptr};
    QPlainTextEdit *m_textEdit{nullptr};
    QPushButton *m_generateButton{nullptr};
    QPushButton *m_lineButton{nullptr};
    QPushButton *m_previewButton{nullptr};
    QPushButton *m_presetsButton{nullptr};
    QPushButton *m_cancelButton{nullptr};
    QProgressBar *m_progress{nullptr};
    QLabel *m_status{nullptr};

    QNetworkAccessManager *m_network{nullptr};
    QNetworkReply *m_currentReply{nullptr};
    QMediaPlayer *m_player{nullptr};
    QAudioOutput *m_audioOutput{nullptr};
    QList<Line> m_queue;
    QStringList m_generatedBinIds;
    QStringList m_generatedTexts;
    QStringList m_generatedImages;
    QList<double> m_generatedDurations;
    QString m_batchStamp;
    int m_total{0};
    int m_done{0};
    int m_failed{0};
    bool m_running{false};
    bool m_cancelled{false};
    bool m_previewOnly{false};
};

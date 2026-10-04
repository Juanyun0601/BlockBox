#ifndef BLURLOADINGOVERLAY_H
#define BLURLOADINGOVERLAY_H

#include <QLabel>
#include <QNetworkAccessManager>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class BlurLoadingOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit BlurLoadingOverlay(QWidget *parent = nullptr);

    void showOverlay(const QString &processName = QString());
    void hideOverlay();
    void updateProgress(int percent, const QString &processName = QString());
    void showError(const QString &errorMessage);
    void clearError();
    void showDiagnosisResult(const QString &result);

signals:
    void diagnoseRequested();

private slots:
    void performDiagnosis();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void initUI();
    void initStyle();
    void captureBlurBackground();
    void adjustGeometryToFullArea();
    void updateLayoutState();

    QPixmap m_blurredBackground;

    QWidget *m_centerWidget;
    QVBoxLayout *m_centerLayout;
    QLabel *m_processLabel;
    QProgressBar *m_progressBar;
    QLabel *m_percentLabel;
    QLabel *m_errorLabel;
    QPushButton *m_diagnoseBtn;
    QWidget *m_progressArea;
    QNetworkAccessManager *m_diagnosisNAM;

    QString m_currentProcessName;
    int m_currentPercent;
    bool m_hasError;
    bool m_isCapturing;
};

#endif

#ifndef APOKOLIPS_SHELL_H
#define APOKOLIPS_SHELL_H

// Shared include block — every split file includes this once.
// Keep the block sorted logically (Qt first, then system).
#include <QApplication>
#include <QPointer>
#include <qtermwidget.h>
#include <QHostInfo>
#include <QThread>
#include <QSplitter>
#include <QRandomGenerator>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QSlider>
#include <QMessageBox>
#include <QVector>
#include <QTransform>
#include <QBuffer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QTimer>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QScreen>
#include <QPixmap>
#include <QProcess>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QWindow>
#include <QMenu>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QListView>
#include <QFileSystemModel>
#include <QStackedWidget>
#include <unistd.h>
#include <QShortcut>
#include <QInputDialog>
#include <QMovie>
#include <QFileDialog>
#include <QScrollArea>
#include <QTextEdit>
#include <QHeaderView>
#include <QTreeView>
#include <QRegularExpression>
#include <QListWidget>
#include <csignal>
#include <QUrl>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QEventLoop>
#include <QDialog>
#include <QCheckBox>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QWidgetAction>
#include <QDate>
#include <QCalendarWidget>
#include <QList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QFileSystemWatcher>
#include <functional>
#include <cmath>




// ----------------------------------------------------------------
// Menu fade-in — install once per process. Every QMenu fades in
// from transparent over 200 ms.
// ----------------------------------------------------------------
class MenuFadeFilter : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject *obj, QEvent *e) override {
        if (e->type() == QEvent::Show) {
            if (auto *m = qobject_cast<QMenu *>(obj)) {
                if (!m->property("_fadeDone").toBool()) {
                    m->setProperty("_fadeDone", true);
                    auto *eff = new QGraphicsOpacityEffect(m);
                    m->setGraphicsEffect(eff);
                    auto *anim = new QPropertyAnimation(eff, "opacity", m);
                    anim->setDuration(200);
                    anim->setStartValue(0.0);
                    anim->setEndValue(1.0);
                    anim->setEasingCurve(QEasingCurve::OutCubic);
                    anim->start(QAbstractAnimation::DeleteWhenStopped);
                    qInfo() << "[menufade] applied";
                }
            }
        }
        return QObject::eventFilter(obj, e);
    }
};

inline void installMenuFade() {
    static bool done = false;
    if (done) return;
    done = true;
    if (qApp) qApp->installEventFilter(new MenuFadeFilter(qApp));
}

#include "widgets.h"
#endif // APOKOLIPS_SHELL_H

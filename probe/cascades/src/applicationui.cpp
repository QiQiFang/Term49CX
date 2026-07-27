#include "applicationui.hpp"

#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/TouchType>

#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>
#include <QDir>

using namespace bb::cascades;

ApplicationUI::ApplicationUI(QObject *parent)
	: QObject(parent)
{
	m_logPath = QLatin1String("/accounts/1000/shared/documents/TouchProbeCascades.log");

	/* Prefer shared Documents; fall back to app data */
	{
		QFile test(m_logPath);
		if (!test.open(QIODevice::WriteOnly | QIODevice::Append)) {
			m_logPath = QDir::homePath() + QLatin1String("/TouchProbeCascades.log");
		} else {
			test.close();
		}
	}

	QmlDocument *qml = QmlDocument::create("asset:///main.qml").parent(this);
	qml->setContextProperty("_app", this);

	AbstractPane *root = qml->createRootObject<AbstractPane>();
	if (!root) {
		logLine("FATAL: createRootObject failed — QML error");
		return;
	}
	Application::instance()->setScene(root);

	logLine(QString("=== Cascades probe start t=%1 ===")
	        .arg(QDateTime::currentMSecsSinceEpoch()));
	logLine(QString("log_path=%1").arg(m_logPath));
	logLine("Soft-swipe physical keyboard; expect TOUCH_KEYBOARD lines");
}

void ApplicationUI::logLine(const QString &line)
{
	qDebug() << line;
	QFile f(m_logPath);
	if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		QTextStream ts(&f);
		ts << line << "\n";
	}
}

void ApplicationUI::onTouchKeyboard(TouchKeyboardEvent *event)
{
	if (!event)
		return;
	const char *tt = "?";
	switch (event->touchType()) {
	case TouchType::Down:   tt = "Down"; break;
	case TouchType::Move:   tt = "Move"; break;
	case TouchType::Up:     tt = "Up"; break;
	case TouchType::Cancel: tt = "Cancel"; break;
	default: break;
	}
	logLine(QString("t=%1 TOUCH_KEYBOARD type=%2 screenX=%3 screenY=%4 fingerId=%5")
	        .arg(QDateTime::currentMSecsSinceEpoch())
	        .arg(QLatin1String(tt))
	        .arg(event->screenX())
	        .arg(event->screenY())
	        .arg(event->fingerId()));
}

void ApplicationUI::onKey(KeyEvent *event)
{
	if (!event)
		return;
	logLine(QString("t=%1 KEY key=%2 pressed=%3")
	        .arg(QDateTime::currentMSecsSinceEpoch())
	        .arg(event->key())
	        .arg(event->isPressed() ? 1 : 0));
}

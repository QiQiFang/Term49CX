#ifndef APPLICATIONUI_HPP_
#define APPLICATIONUI_HPP_

#include <QObject>
#include <bb/cascades/TouchKeyboardEvent>
#include <bb/cascades/KeyEvent>

class ApplicationUI : public QObject
{
	Q_OBJECT
public:
	explicit ApplicationUI(QObject *parent = 0);
	virtual ~ApplicationUI() {}

	Q_INVOKABLE void logLine(const QString &line);
	Q_INVOKABLE void onTouchKeyboard(bb::cascades::TouchKeyboardEvent *event);
	Q_INVOKABLE void onKey(bb::cascades::KeyEvent *event);

private:
	QString m_logPath;
};

#endif

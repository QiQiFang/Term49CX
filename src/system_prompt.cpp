#include "system_prompt.h"

#include <stdlib.h>
#include <string.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QByteArray>

#include <bb/system/SystemPrompt>
#include <bb/system/SystemUiButton>
#include <bb/system/SystemUiInputField>
#include <bb/system/SystemUiResult>
#include <bb/system/SystemUiReturnKeyAction>

using namespace bb::system;

extern "C" int system_prompt_init(int *argc, char **argv)
{
	if (QCoreApplication::instance() != NULL)
		return 0;
	static QCoreApplication application(*argc, argv);
	return QCoreApplication::instance() == &application ? 0 : -1;
}

extern "C" int system_prompt_run(char **utf8_text)
{
	*utf8_text = NULL;
	SystemPrompt prompt;
	prompt.setTitle(QString::fromUtf8("Enter text"));
	prompt.setBody(QString::fromUtf8("Use the system input method, then tap Send to enter text in the terminal."));
	prompt.confirmButton()->setLabel(QString::fromUtf8("Send"));
	prompt.cancelButton()->setLabel(QString::fromUtf8("Cancel"));
	prompt.setDefaultButton(prompt.confirmButton());
	prompt.setReturnKeyAction(SystemUiReturnKeyAction::Send);
	prompt.inputField()->setEmptyText(QString::fromUtf8("Type here"));
	prompt.inputField()->setMaximumLength(2048);

	QEventLoop wait_for_result;
	if (!QObject::connect(&prompt, SIGNAL(finished(bb::system::SystemUiResult::Type)),
	                     &wait_for_result, SLOT(quit())))
		return -1;
	prompt.show();
	if (prompt.result() == SystemUiResult::None)
		wait_for_result.exec();

	if (prompt.result() == SystemUiResult::CancelButtonSelection ||
	    prompt.result() == SystemUiResult::None)
		return 0;
	if (prompt.result() != SystemUiResult::ConfirmButtonSelection)
		return -1;
	QByteArray bytes = prompt.inputFieldTextEntry().toUtf8();
	char *copy = (char *)malloc((size_t)bytes.size() + 1);
	if (copy == NULL)
		return -1;
	memcpy(copy, bytes.constData(), (size_t)bytes.size());
	copy[bytes.size()] = '\0';
	*utf8_text = copy;
	return 1;
}

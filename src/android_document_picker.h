#pragma once

#include <QObject>
#include <QString>

#include <functional>

// Android's system document picker (ACTION_OPEN_DOCUMENT), bypassing Qt
// Quick's FileDialog: on Android that dialog checks the picked content://
// URI with QFileInfo::exists(), which fails, and drops the selection (see
// QQuickFileDialog::setSelectedFiles()). The document is read through the
// ContentResolver (org.aowis.mobile.DocumentPicker).
namespace AndroidDocumentPicker
{

enum class Outcome { Picked, Cancelled, Unreadable, Denied, NotFound, TooLarge, Unavailable };

// True on Android; elsewhere pickText() reports Unavailable.
bool available();

// Opens the picker; done(outcome, display name, UTF-8 text) runs later on
// context's thread, never after context is destroyed. At most max_bytes.
// For Unreadable, text contains a diagnostic without document contents.
void pickText(QObject *context, int max_bytes,
              std::function<void(Outcome, const QString &name, const QString &text)> done);

} // namespace AndroidDocumentPicker

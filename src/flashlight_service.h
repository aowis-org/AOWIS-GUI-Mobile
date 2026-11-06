#pragma once

#include <QObject>
#include <QString>

// Android torch access is independent of Qt Multimedia and the map renderer.
// Never request camera permission until the user turns the flashlight on.
class FlashlightService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit FlashlightService(QObject *parent = nullptr);
    ~FlashlightService() override;

    bool available() const;
    bool enabled() const;
    bool busy() const;
    QString error() const;

    Q_INVOKABLE void toggle();
    void setApplicationActive(bool active);

signals:
    void changed();

private:
    void setTorch(bool enabled);
    void setError(const QString &message);
    void refreshAvailability();

    bool m_available = false;
    bool m_enabled = false;
    bool m_busy = false;
    bool m_applicationActive = false;
    QString m_error;
};

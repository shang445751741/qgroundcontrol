/****************************************************************************
 *
 *   (c) 2009-2016 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#ifndef VideoSettings_H
#define VideoSettings_H

#include "SettingsGroup.h"

class VideoSettings : public SettingsGroup
{
    Q_OBJECT

public:
    VideoSettings(QObject* parent = NULL);

    Q_PROPERTY(Fact* videoSource            READ videoSource            CONSTANT)
    Q_PROPERTY(Fact* udpPort                READ udpPort                CONSTANT)
    Q_PROPERTY(Fact* tcpUrl                 READ tcpUrl                 CONSTANT)
    Q_PROPERTY(Fact* rtspUrl                READ rtspUrl                CONSTANT)
    Q_PROPERTY(Fact* aspectRatio            READ aspectRatio            CONSTANT)
    Q_PROPERTY(Fact* gridLines              READ gridLines              CONSTANT)
    Q_PROPERTY(Fact* showRecControl         READ showRecControl         CONSTANT)
    Q_PROPERTY(Fact* recordingFormat        READ recordingFormat        CONSTANT)
    Q_PROPERTY(Fact* maxVideoSize           READ maxVideoSize           CONSTANT)
    Q_PROPERTY(Fact* enableStorageLimit     READ enableStorageLimit     CONSTANT)
    Q_PROPERTY(Fact* rtspTimeout            READ rtspTimeout            CONSTANT)
    Q_PROPERTY(Fact* streamEnabled          READ streamEnabled          CONSTANT)
    Q_PROPERTY(Fact* disableWhenDisarmed    READ disableWhenDisarmed    CONSTANT)
    Q_PROPERTY(Fact* videoResolution        READ videoResolution        CONSTANT)
    Q_PROPERTY(Fact* cameraId               READ cameraId               CONSTANT)
    Q_PROPERTY(bool  streamConfigured       READ streamConfigured       NOTIFY streamConfiguredChanged)

    Fact* videoSource           (void);
    Fact* udpPort               (void);
    Fact* rtspUrl               (void);
    Fact* tcpUrl                (void);
    Fact* aspectRatio           (void);
    Fact* gridLines             (void);
    Fact* showRecControl        (void);
    Fact* recordingFormat       (void);
    Fact* maxVideoSize          (void);
    Fact* enableStorageLimit    (void);
    Fact* rtspTimeout           (void);
    Fact* streamEnabled         (void);
    Fact* disableWhenDisarmed   (void);
    Fact* videoResolution       (void);
    Fact* cameraId              (void);
    bool  streamConfigured      (void);

    static const char* videoSettingsGroupName;

    static const char* videoSourceName;
    static const char* udpPortName;
    static const char* rtspUrlName;
    static const char* tcpUrlName;
    static const char* videoAspectRatioName;
    static const char* videoGridLinesName;
    static const char* showRecControlName;
    static const char* recordingFormatName;
    static const char* maxVideoSizeName;
    static const char* enableStorageLimitName;
    static const char* rtspTimeoutName;
    static const char* streamEnabledName;
    static const char* disableWhenDisarmedName;
    static const char* videoResolutionName;
    static const char* cameraIdName;

    static const char* videoSourceNoVideo;
    static const char* videoDisabled;
    static const char* videoSourceUDP;
    static const char* videoSourceRTSP;
    static const char* videoSourceTCP;
    static const char* videoSourceAuto;

signals:
    void streamConfiguredChanged    ();

private slots:
    void _configChanged             (QVariant value);

private:
    SettingsFact* _videoSourceFact;
    SettingsFact* _udpPortFact;
    SettingsFact* _tcpUrlFact;
    SettingsFact* _rtspUrlFact;
    SettingsFact* _videoAspectRatioFact;
    SettingsFact* _gridLinesFact;
    SettingsFact* _showRecControlFact;
    SettingsFact* _recordingFormatFact;
    SettingsFact* _maxVideoSizeFact;
    SettingsFact* _enableStorageLimitFact;
    SettingsFact* _rtspTimeoutFact;
    SettingsFact* _streamEnabledFact;
    SettingsFact* _disableWhenDisarmedFact;
    SettingsFact* _videoResolutionFact;
    SettingsFact* _cameraIdFact;
};

#endif

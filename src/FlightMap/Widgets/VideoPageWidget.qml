/****************************************************************************
 *
 *   (c) 2009-2016 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick                  2.4
import QtPositioning            5.2
import QtQuick.Layouts          1.2
import QtQuick.Controls         1.4
import QtQuick.Dialogs          1.2
import QtGraphicalEffects       1.0

import QGroundControl                   1.0
import QGroundControl.ScreenTools       1.0
import QGroundControl.Controls          1.0
import QGroundControl.Palette           1.0
import QGroundControl.Vehicle           1.0
import QGroundControl.Controllers       1.0
import QGroundControl.FactSystem        1.0
import QGroundControl.FactControls      1.0

/// Video streaming page for Instrument Panel PageView
Item {
    width:              pageWidth
    height:             videoGrid.height + (ScreenTools.defaultFontPixelHeight * 2)
    anchors.margins:    ScreenTools.defaultFontPixelWidth * 2
    anchors.centerIn:   parent

    property var    _activeVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property bool   _communicationLost:     _activeVehicle ? _activeVehicle.connectionLost : false
    property var    _videoReceiver:         QGroundControl.videoManager.videoReceiver
    property bool   _recordingVideo:        _videoReceiver && _videoReceiver.recording
    property bool   _videoRunning:          _videoReceiver && _videoReceiver.videoRunning
    property bool   _streamingEnabled:      QGroundControl.settingsManager.videoSettings.streamConfigured

    QGCPalette { id:qgcPal; colorGroupEnabled: true }

    GridLayout {
        id:                 videoGrid
        columns:            2
        columnSpacing:      ScreenTools.defaultFontPixelWidth * 2
        rowSpacing:         ScreenTools.defaultFontPixelHeight
        anchors.centerIn:   parent
        Connections {
            // For some reason, the normal signal is not reflected in the control below
            target: QGroundControl.settingsManager.videoSettings.streamEnabled
            onRawValueChanged: {
                enableSwitch.checked = QGroundControl.settingsManager.videoSettings.streamEnabled.rawValue
            }
        }
        // Enable/Disable Video Streaming
        QGCLabel {
           text:            qsTr("Enable Stream")
           font.pointSize:  ScreenTools.smallFontPointSize
        }
        QGCSwitch {
            id:             enableSwitch
            enabled:        _streamingEnabled
            checked:        QGroundControl.settingsManager.videoSettings.streamEnabled.rawValue
            onCheckedChanged: {
                if(checked) {
                    QGroundControl.settingsManager.videoSettings.streamEnabled.rawValue = 1
                    _videoReceiver.start()
                } else {
                    QGroundControl.settingsManager.videoSettings.streamEnabled.rawValue = 0
                    _videoReceiver.stop()
                }
            }
        }
        // camera id
        QGCLabel {
           visible:        QGroundControl.videoManager.videoStreamControl.cameraCount > 1
           text:            qsTr("Camera Switch")
           font.pointSize:  ScreenTools.smallFontPointSize
        }
        QGCSwitch {
            id:             cameraIdSwitch
            visible:        QGroundControl.videoManager.videoStreamControl.cameraCount > 1
            enabled:        !QGroundControl.videoManager.videoStreamControl.settingInProgress
            checked:        QGroundControl.settingsManager.videoSettings.cameraId.rawValue === 1
            onCheckedChanged: {
                if(checked) {
                    QGroundControl.settingsManager.videoSettings.cameraId.rawValue = 1
                } else {
                    QGroundControl.settingsManager.videoSettings.cameraId.rawValue = 0
                }
            }
        }
        // resolution
        QGCLabel {
           text:            qsTr("1080P video")
           font.pointSize:  ScreenTools.smallFontPointSize
        }
        QGCSwitch {
            id:             fhdSwitch
            enabled:        !QGroundControl.videoManager.videoStreamControl.settingInProgress
            checked:        (QGroundControl.settingsManager.videoSettings.videoResolution.rawValue === 2)
                            || ((QGroundControl.settingsManager.videoSettings.videoResolution.rawValue === 0)
                                &&(QGroundControl.videoManager.videoStreamControl.videoResolution === "1920x1080"))
            onCheckedChanged: {
                QGroundControl.videoManager.videoStreamControl.fhdEnabledChanged(checked)
            }
        }
        // Grid Lines
        QGCLabel {
           text:            qsTr("Grid Lines")
           font.pointSize:  ScreenTools.smallFontPointSize
           visible:         QGroundControl.videoManager.isGStreamer && QGroundControl.settingsManager.videoSettings.gridLines.visible
        }
        QGCSwitch {
            enabled:        _streamingEnabled && _activeVehicle
            checked:        QGroundControl.settingsManager.videoSettings.gridLines.rawValue
            visible:        QGroundControl.videoManager.isGStreamer && QGroundControl.settingsManager.videoSettings.gridLines.visible
            onCheckedChanged: {
                if(checked) {
                    QGroundControl.settingsManager.videoSettings.gridLines.rawValue = 1
                } else {
                    QGroundControl.settingsManager.videoSettings.gridLines.rawValue = 0
                }
            }
        }
        QGCLabel {
           text:            _recordingVideo ? qsTr("Stop Recording") : qsTr("Record Stream")
           font.pointSize:  ScreenTools.smallFontPointSize
           visible:         QGroundControl.settingsManager.videoSettings.showRecControl.rawValue
        }
        // Button to start/stop video recording
        Item {
            anchors.margins:    ScreenTools.defaultFontPixelHeight / 2
            height:             ScreenTools.defaultFontPixelHeight * 2
            width:              height
            Layout.alignment:   Qt.AlignHCenter
            visible:            QGroundControl.settingsManager.videoSettings.showRecControl.rawValue
            Rectangle {
                id:                 recordBtnBackground
                anchors.top:        parent.top
                anchors.bottom:     parent.bottom
                width:              height
                radius:             _recordingVideo ? 0 : height
                color:              (_videoRunning && _streamingEnabled) ? "red" : "gray"
                SequentialAnimation on opacity {
                    running:        _recordingVideo
                    loops:          Animation.Infinite
                    PropertyAnimation { to: 0.5; duration: 500 }
                    PropertyAnimation { to: 1.0; duration: 500 }
                }
            }
            QGCColoredImage {
                anchors.top:                parent.top
                anchors.bottom:             parent.bottom
                anchors.horizontalCenter:   parent.horizontalCenter
                width:                      height * 0.625
                sourceSize.width:           width
                source:                     "/qmlimages/CameraIcon.svg"
                visible:                    recordBtnBackground.visible
                fillMode:                   Image.PreserveAspectFit
                color:                      "white"
            }
            MouseArea {
                anchors.fill:   parent
                enabled:        _videoRunning && _streamingEnabled
                onClicked: {
                    if (_recordingVideo) {
                        _videoReceiver.stopRecording()
                        // reset blinking animation
                        recordBtnBackground.opacity = 1
                    } else {
                        _videoReceiver.startRecording()
                    }
                }
            }
        }
        QGCLabel {
            text:               qsTr("Video Streaming Not Configured")
            font.pointSize:     ScreenTools.smallFontPointSize
            visible:            !_streamingEnabled
            Layout.columnSpan:  2
        }
    }
}

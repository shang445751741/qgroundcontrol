﻿/****************************************************************************
 *
 *   (c) 2009-2016 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QGeoPositionInfoSource>
#include <QNmeaPositionInfoSource>

#include <QVariant>

#include "QGCToolbox.h"
#include "SimulatedPosition.h"

class QGCPositionManager : public QGCTool {
    Q_OBJECT

public:

    QGCPositionManager(QGCApplication* app, QGCToolbox* toolbox);
    ~QGCPositionManager();

    enum QGCPositionSource {
        Simulated,
        InternalGPS,
        Log,
        NmeaGPS
    };

    void setPositionSource(QGCPositionSource source);

    int updateInterval() const;

    void setToolbox(QGCToolbox* toolbox);

    void setNmeaSourceDevice(QIODevice* device);
    void stopPositionUpdate();
    void startPositionUpdate();

private slots:
    void _positionUpdated(const QGeoPositionInfo &update);
    void _error(QGeoPositionInfoSource::Error positioningError);
    void _handleApplicationStateChanged(Qt::ApplicationState state);

signals:
    void lastPositionUpdated(bool valid, QVariant lastPosition);
    void positionInfoUpdated(QGeoPositionInfo update);

private:
    int _updateInterval;
    QGeoPositionInfoSource * _currentSource;
    QGeoPositionInfoSource * _defaultSource;
    QNmeaPositionInfoSource * _nmeaSource;
    QGeoPositionInfoSource * _simulatedSource;
};

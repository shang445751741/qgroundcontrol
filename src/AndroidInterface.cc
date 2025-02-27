/****************************************************************************
 *
 * Copyright (C) 2018 Pinecone Inc. All rights reserved.
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include <QtAndroidExtras/QtAndroidExtras>
#include <QtAndroidExtras/QAndroidJniObject>
#include "QGCApplication.h"
#include "AndroidInterface.h"
#include <QAndroidJniObject>

void AndroidInterface::acquireScreenWakeLock()
{
    QAndroidJniObject::callStaticMethod<void>("org/mavlink/qgroundcontrol/QGCActivity", "acquireScreenWakeLock");
}

void AndroidInterface::releaseScreenWakeLock() {
    QAndroidJniObject::callStaticMethod<void>("org/mavlink/qgroundcontrol/QGCActivity", "releaseScreenWakeLock");
}

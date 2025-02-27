/****************************************************************************
 *
 *   (c) 2009-2016 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


#include "MissionCommandUIInfo.h"
#include "MissionController.h"
#include "MultiVehicleManager.h"
#include "MissionManager.h"
#include "CoordinateVector.h"
#include "FirmwarePlugin.h"
#include "QGCApplication.h"
#include "SimpleMissionItem.h"
#include "SurveyMissionItem.h"
#include "FixedWingLandingComplexItem.h"
#include "StructureScanComplexItem.h"
#include "StructureScanComplexItem.h"
#include "JsonHelper.h"
#include "ParameterManager.h"
#include "QGroundControlQmlGlobal.h"
#include "SettingsManager.h"
#include "AppSettings.h"
#include "MissionSettingsItem.h"
#include "QGCQGeoCoordinate.h"
#include "PlanMasterController.h"
#include "KML.h"

#ifndef __mobile__
#include "MainWindow.h"
#include "QGCQFileDialog.h"
#endif

QGC_LOGGING_CATEGORY(MissionControllerLog, "MissionControllerLog")

const char* MissionController::_settingsGroup =                 "MissionController";
const char* MissionController::_jsonFileTypeValue =             "Mission";
const char* MissionController::_jsonItemsKey =                  "items";
const char* MissionController::_jsonPlannedHomePositionKey =    "plannedHomePosition";
const char* MissionController::_jsonFirmwareTypeKey =           "firmwareType";
const char* MissionController::_jsonVehicleTypeKey =            "vehicleType";
const char* MissionController::_jsonCruiseSpeedKey =            "cruiseSpeed";
const char* MissionController::_jsonHoverSpeedKey =             "hoverSpeed";
const char* MissionController::_jsonParamsKey =                 "params";

// Deprecated V1 format keys
const char* MissionController::_jsonComplexItemsKey =           "complexItems";
const char* MissionController::_jsonMavAutopilotKey =           "MAV_AUTOPILOT";

const int   MissionController::_missionFileVersion =            2;

MissionController::MissionController(PlanMasterController* masterController, QObject *parent)
    : PlanElementController(masterController, parent)
    , _missionManager(_managerVehicle->missionManager())
    , _visualItems(NULL)
    , _settingsItem(NULL)
    , _firstItemsFromVehicle(false)
    , _itemsRequested(false)
    , _surveyMissionItemName(tr("Survey"))
    , _fwLandingMissionItemName(tr("Fixed Wing Landing"))
    , _structureScanMissionItemName(tr("Structure Scan"))
    , _appSettings(qgcApp()->toolbox()->settingsManager()->appSettings())
    , _progressPct(0)
    , _currentPlanViewIndex(-1)
    , _currentPlanViewItem(NULL)
{
    _resetMissionFlightStatus();
    managerVehicleChanged(_managerVehicle);
}

MissionController::~MissionController()
{

}

void MissionController::_resetMissionFlightStatus(void)
{
    _missionFlightStatus.totalDistance =        0.0;
    _missionFlightStatus.maxTelemetryDistance = 0.0;
    _missionFlightStatus.totalTime =            0.0;
    _missionFlightStatus.hoverTime =            0.0;
    _missionFlightStatus.cruiseTime =           0.0;
    _missionFlightStatus.hoverDistance =        0.0;
    _missionFlightStatus.cruiseDistance =       0.0;
    _missionFlightStatus.cruiseSpeed =          _controllerVehicle->defaultCruiseSpeed();
    _missionFlightStatus.hoverSpeed =           _controllerVehicle->defaultHoverSpeed();
    _missionFlightStatus.vehicleSpeed =         _controllerVehicle->multiRotor() || _managerVehicle->vtol() ? _missionFlightStatus.hoverSpeed : _missionFlightStatus.cruiseSpeed;
    _missionFlightStatus.vehicleYaw =           0.0;
    _missionFlightStatus.gimbalYaw =            std::numeric_limits<double>::quiet_NaN();
    _missionFlightStatus.gimbalPitch =          std::numeric_limits<double>::quiet_NaN();

    // Battery information

    _missionFlightStatus.mAhBattery =           0;
    _missionFlightStatus.hoverAmps =            0;
    _missionFlightStatus.cruiseAmps =           0;
    _missionFlightStatus.ampMinutesAvailable =  0;
    _missionFlightStatus.hoverAmpsTotal =       0;
    _missionFlightStatus.cruiseAmpsTotal =      0;
    _missionFlightStatus.batteryChangePoint =   -1;
    _missionFlightStatus.batteriesRequired =    -1;

    _controllerVehicle->firmwarePlugin()->batteryConsumptionData(_controllerVehicle, _missionFlightStatus.mAhBattery, _missionFlightStatus.hoverAmps, _missionFlightStatus.cruiseAmps);
    if (_missionFlightStatus.mAhBattery != 0) {
        double batteryPercentRemainingAnnounce = qgcApp()->toolbox()->settingsManager()->appSettings()->batteryPercentRemainingAnnounce()->rawValue().toDouble();
        _missionFlightStatus.ampMinutesAvailable = (double)_missionFlightStatus.mAhBattery / 1000.0 * 60.0 * ((100.0 - batteryPercentRemainingAnnounce) / 100.0);
    }

    emit missionDistanceChanged(_missionFlightStatus.totalDistance);
    emit missionTimeChanged();
    emit missionHoverDistanceChanged(_missionFlightStatus.hoverDistance);
    emit missionCruiseDistanceChanged(_missionFlightStatus.cruiseDistance);
    emit missionHoverTimeChanged();
    emit missionCruiseTimeChanged();
    emit missionMaxTelemetryChanged(_missionFlightStatus.maxTelemetryDistance);
    emit batteryChangePointChanged(_missionFlightStatus.batteryChangePoint);
    emit batteriesRequiredChanged(_missionFlightStatus.batteriesRequired);

}

void MissionController::start(bool editMode)
{
    qCDebug(MissionControllerLog) << "start editMode" << editMode;

    PlanElementController::start(editMode);
    _init();
}

void MissionController::_init(void)
{
    // We start with an empty mission
    _visualItems = new QmlObjectListModel(this);
    _addMissionSettings(_visualItems, false /* addToCenter */);
    _initAllVisualItems();
}

// Called when new mission items have completed downloading from Vehicle
void MissionController::_newMissionItemsAvailableFromVehicle(bool removeAllRequested)
{
    qCDebug(MissionControllerLog) << "_newMissionItemsAvailableFromVehicle";

    // Fly view always reloads on _loadComplete
    // Plan view only reloads on _loadComplete if specifically requested
    if (!_editMode || removeAllRequested || _itemsRequested) {
        // Fly Mode (accept if):
        //      - Always accepts new items from the vehicle so Fly view is kept up to date
        // Edit Mode (accept if):
        //      - Either a load from vehicle was manually requested or
        //      - The initial automatic load from a vehicle completed and the current editor is empty
        //      - Remove all way requested from Fly view (clear mission on flight end)

        QmlObjectListModel* newControllerMissionItems = new QmlObjectListModel(this);
        const QList<MissionItem*>& newMissionItems = _missionManager->missionItems();
        qCDebug(MissionControllerLog) << "loading from vehicle: count"<< newMissionItems.count();

        int i = 0;
        if (_controllerVehicle->firmwarePlugin()->sendHomePositionToVehicle() && newMissionItems.count() != 0) {
            // First item is fake home position
            _addMissionSettings(newControllerMissionItems, false /* addToCenter */);
            MissionSettingsItem* settingsItem = newControllerMissionItems->value<MissionSettingsItem*>(0);
            if (!settingsItem) {
                qWarning() << "First item is not settings item";
                return;
            }
            MissionItem* fakeHomeItem = newMissionItems[0];
            if (fakeHomeItem->coordinate().latitude() != 0 || fakeHomeItem->coordinate().longitude() != 0) {
                settingsItem->setCoordinate(fakeHomeItem->coordinate());
            }
            i = 1;
        }

        for (; i<newMissionItems.count(); i++) {
            const MissionItem* missionItem = newMissionItems[i];
            newControllerMissionItems->append(new SimpleMissionItem(_controllerVehicle, _editMode, *missionItem, this));
        }

        _deinitAllVisualItems();
        _visualItems->deleteLater();
        _settingsItem = NULL;
        _visualItems = NULL;
        _updateContainsItems(); // This will clear containsItems which will be set again below. This will re-pop Start Mission confirmation.
        _visualItems = newControllerMissionItems;

        if (!_controllerVehicle->firmwarePlugin()->sendHomePositionToVehicle() || _visualItems->count() == 0) {
            _addMissionSettings(_visualItems, _editMode && _visualItems->count() > 0 /* addToCenter */);
        }

        MissionController::_scanForAdditionalSettings(_visualItems, _controllerVehicle);

        _initAllVisualItems();
        _updateContainsItems();
        emit newItemsFromVehicle();
    }
    _itemsRequested = false;
}

void MissionController::loadFromVehicle(void)
{
    if (_masterController->offline()) {
        qCWarning(MissionControllerLog) << "MissionControllerLog::loadFromVehicle called while offline";
    } else if (syncInProgress()) {
        qCWarning(MissionControllerLog) << "MissionControllerLog::loadFromVehicle called while syncInProgress";
    } else {
        _itemsRequested = true;
        _managerVehicle->missionManager()->loadFromVehicle();
    }
}

void MissionController::sendToVehicle(void)
{
    if (_masterController->offline()) {
        qCWarning(MissionControllerLog) << "MissionControllerLog::sendToVehicle called while offline";
    } else if (syncInProgress()) {
        qCWarning(MissionControllerLog) << "MissionControllerLog::sendToVehicle called while syncInProgress";
    } else {
        qCDebug(MissionControllerLog) << "MissionControllerLog::sendToVehicle";
        if (_visualItems->count() == 1) {
            // This prevents us from sending a possibly bogus home position to the vehicle
            QmlObjectListModel emptyModel;
            sendItemsToVehicle(_managerVehicle, &emptyModel);
        } else {
            sendItemsToVehicle(_managerVehicle, _visualItems);
        }
        setDirty(false);
    }
}

/// Converts from visual items to MissionItems
///     @param missionItemParent QObject parent for newly allocated MissionItems
/// @return true: Mission end action was added to end of list
bool MissionController::_convertToMissionItems(QmlObjectListModel* visualMissionItems, QList<MissionItem*>& rgMissionItems, QObject* missionItemParent)
{
    if (visualMissionItems->count() == 0) {
        return false;
    }

    bool endActionSet = false;
    int lastSeqNum = 0;

    for (int i=0; i<visualMissionItems->count(); i++) {
        VisualMissionItem* visualItem = qobject_cast<VisualMissionItem*>(visualMissionItems->get(i));

        lastSeqNum = visualItem->lastSequenceNumber();
        visualItem->appendMissionItems(rgMissionItems, missionItemParent);

        qCDebug(MissionControllerLog) << "_convertToMissionItems seqNum:lastSeqNum:command"
                                      << visualItem->sequenceNumber()
                                      << lastSeqNum
                                      << visualItem->commandName();
    }

    // Mission settings has a special case for end mission action
    MissionSettingsItem* settingsItem = visualMissionItems->value<MissionSettingsItem*>(0);
    if (settingsItem) {
        endActionSet = settingsItem->addMissionEndAction(rgMissionItems, lastSeqNum + 1, missionItemParent);
    }

    return endActionSet;
}

void MissionController::convertToKMLDocument(QDomDocument& document)
{
    QJsonObject missionJson;
    QmlObjectListModel* visualItems = new QmlObjectListModel();
    QList<MissionItem*> missionItems;
    QString error;
    save(missionJson);
    _loadItemsFromJson(missionJson, visualItems, error);
    _convertToMissionItems(visualItems, missionItems, this);

    if (missionItems.count() == 0) {
        return;
    }

    float altitude = missionJson[_jsonPlannedHomePositionKey].toArray()[2].toDouble();

    QString coord;
    QStringList coords;
    // Drop home position
    bool dropPoint = true;
    for(const auto& item : missionItems) {
        if(dropPoint) {
            dropPoint = false;
            continue;
        }
        const MissionCommandUIInfo* uiInfo = \
                qgcApp()->toolbox()->missionCommandTree()->getUIInfo(_controllerVehicle, item->command());

        if (uiInfo && uiInfo->specifiesCoordinate() && !uiInfo->isStandaloneCoordinate()) {
            coord = QString::number(item->param6(),'f',7) \
                    + "," \
                    + QString::number(item->param5(),'f',7) \
                    + "," \
                    + QString::number(item->param7() + altitude,'f',2);
            coords.append(coord);
        }
    }
    Kml kml;
    kml.points(coords);
    kml.save(document);
}

void MissionController::sendItemsToVehicle(Vehicle* vehicle, QmlObjectListModel* visualMissionItems)
{
    if (vehicle) {
        QList<MissionItem*> rgMissionItems;

        _convertToMissionItems(visualMissionItems, rgMissionItems, vehicle);

        // PlanManager takes control of MissionItems so no need to delete
        vehicle->missionManager()->writeMissionItems(rgMissionItems);
    }
}

int MissionController::_nextSequenceNumber(void)
{
    if (_visualItems->count() == 0) {
        qWarning() << "Internal error: Empty visual item list";
        return 0;
    } else {
        VisualMissionItem* lastItem = _visualItems->value<VisualMissionItem*>(_visualItems->count() - 1);
        return lastItem->lastSequenceNumber() + 1;
    }
}

int MissionController::insertSimpleMissionItem(QGeoCoordinate coordinate, int i)
{
    int sequenceNumber = _nextSequenceNumber();
    SimpleMissionItem * newItem = new SimpleMissionItem(_controllerVehicle, this);
    newItem->setSequenceNumber(sequenceNumber);
    newItem->setCoordinate(coordinate);
    newItem->setCommand(MavlinkQmlSingleton::MAV_CMD_NAV_WAYPOINT);
    _initVisualItem(newItem);
    if (_visualItems->count() == 1) {
        MavlinkQmlSingleton::Qml_MAV_CMD takeoffCmd = _controllerVehicle->vtol() ? MavlinkQmlSingleton::MAV_CMD_NAV_VTOL_TAKEOFF : MavlinkQmlSingleton::MAV_CMD_NAV_TAKEOFF;
        if (_controllerVehicle->firmwarePlugin()->supportedMissionCommands().contains((MAV_CMD)takeoffCmd)) {
            newItem->setCommand(takeoffCmd);
        }
    }
    newItem->setDefaultsForCommand();
    if ((MAV_CMD)newItem->command() == MAV_CMD_NAV_WAYPOINT) {
        double      prevAltitude;
        MAV_FRAME   prevFrame;

        if (_findPreviousAltitude(i, &prevAltitude, &prevFrame)) {
            newItem->missionItem().setFrame(prevFrame);
            newItem->missionItem().setParam7(prevAltitude);
        }
    }
    newItem->setMissionFlightStatus(_missionFlightStatus);
    _visualItems->insert(i, newItem);

    _recalcAll();

    return newItem->sequenceNumber();
}

int MissionController::insertROIMissionItem(QGeoCoordinate coordinate, int i)
{
    int sequenceNumber = _nextSequenceNumber();
    SimpleMissionItem * newItem = new SimpleMissionItem(_controllerVehicle, this);
    newItem->setSequenceNumber(sequenceNumber);
    newItem->setCommand((MavlinkQmlSingleton::Qml_MAV_CMD)(_controllerVehicle->firmwarePlugin()->supportedMissionCommands().contains((MAV_CMD)MAV_CMD_DO_SET_ROI_LOCATION) ?
        MAV_CMD_DO_SET_ROI_LOCATION :
        MAV_CMD_DO_SET_ROI));
    _initVisualItem(newItem);
    newItem->setDefaultsForCommand();
    newItem->setCoordinate(coordinate);

    double      prevAltitude;
    MAV_FRAME   prevFrame;

    if (_findPreviousAltitude(i, &prevAltitude, &prevFrame)) {
        newItem->missionItem().setFrame(prevFrame);
        newItem->missionItem().setParam7(prevAltitude);
    }
    _visualItems->insert(i, newItem);

    _recalcAll();

    return newItem->sequenceNumber();
}

int MissionController::insertComplexMissionItem(QString itemName, QGeoCoordinate mapCenterCoordinate, int i)
{
    ComplexMissionItem* newItem;

    int sequenceNumber = _nextSequenceNumber();
    if (itemName == _surveyMissionItemName) {
        newItem = new SurveyMissionItem(_controllerVehicle, _visualItems);
        newItem->setCoordinate(mapCenterCoordinate);
        // If the vehicle is known to have a gimbal then we automatically point the gimbal straight down if not already set
        bool rollSupported = false;
        bool pitchSupported = false;
        bool yawSupported = false;
        MissionSettingsItem* settingsItem = _visualItems->value<MissionSettingsItem*>(0);
        CameraSection* cameraSection = settingsItem->cameraSection();
        // Set camera to photo mode (leave alone if user already specified)
        if (cameraSection->cameraModeSupported() && !cameraSection->specifyCameraMode()) {
            cameraSection->setSpecifyCameraMode(true);
            cameraSection->cameraMode()->setRawValue(CAMERA_MODE_IMAGE_SURVEY);
        }
        // Point gimbal straight down
        if (_controllerVehicle->firmwarePlugin()->hasGimbal(_controllerVehicle, rollSupported, pitchSupported, yawSupported) && pitchSupported) {
            // If the user already specified a gimbal angle leave it alone
            if (!cameraSection->specifyGimbal()) {
                cameraSection->setSpecifyGimbal(true);
                cameraSection->gimbalPitch()->setRawValue(-90.0);
            }
        }
    } else if (itemName == _fwLandingMissionItemName) {
        newItem = new FixedWingLandingComplexItem(_controllerVehicle, _visualItems);
    } else if (itemName == _structureScanMissionItemName) {
        newItem = new StructureScanComplexItem(_controllerVehicle, _visualItems);
    } else {
        qWarning() << "Internal error: Unknown complex item:" << itemName;
        return sequenceNumber;
    }
    newItem->setSequenceNumber(sequenceNumber);
    _initVisualItem(newItem);

    _visualItems->insert(i, newItem);

    _recalcAll();

    return newItem->sequenceNumber();
}

void MissionController::removeMissionItem(int index)
{
    if (index <= 0 || index >= _visualItems->count()) {
        qWarning() << "MissionController::removeMissionItem called with bad index - count:index" << _visualItems->count() << index;
        return;
    }

    bool surveyRemoved = _visualItems->value<SurveyMissionItem*>(index);
    VisualMissionItem* item = qobject_cast<VisualMissionItem*>(_visualItems->removeAt(index));

    _deinitVisualItem(item);
    item->deleteLater();

    if (surveyRemoved) {
        // Determine if the mission still has another survey in it
        bool foundSurvey = false;
        for (int i=1; i<_visualItems->count(); i++) {
            if (_visualItems->value<SurveyMissionItem*>(i)) {
                foundSurvey = true;
                break;
            }
        }

        // If there is no longer a survey item in the mission remove added commands
        if (!foundSurvey) {
            bool rollSupported = false;
            bool pitchSupported = false;
            bool yawSupported = false;
            MissionSettingsItem* settingsItem = _visualItems->value<MissionSettingsItem*>(0);
            CameraSection* cameraSection = settingsItem->cameraSection();
            if (_controllerVehicle->firmwarePlugin()->hasGimbal(_controllerVehicle, rollSupported, pitchSupported, yawSupported) && pitchSupported) {
                if (cameraSection->specifyGimbal() && cameraSection->gimbalPitch()->rawValue().toDouble() == -90.0 && cameraSection->gimbalYaw()->rawValue().toDouble() == 0.0) {
                    cameraSection->setSpecifyGimbal(false);
                }
            }
            if (cameraSection->cameraModeSupported() && cameraSection->specifyCameraMode() && cameraSection->cameraMode()->rawValue().toInt() == 0) {
                cameraSection->setSpecifyCameraMode(false);
            }
        }
    }

    _recalcAll();
    setDirty(true);
}

void MissionController::removeAll(void)
{
    if (_visualItems) {
        _deinitAllVisualItems();
        _visualItems->clearAndDeleteContents();
        _visualItems->deleteLater();
        _settingsItem = NULL;
        _visualItems = new QmlObjectListModel(this);
        _addMissionSettings(_visualItems, false /* addToCenter */);
        _initAllVisualItems();
        setDirty(true);
        _resetMissionFlightStatus();
    }
}

bool MissionController::_loadJsonMissionFileV1(const QJsonObject& json, QmlObjectListModel* visualItems, QString& errorString)
{
    // Validate root object keys
    QList<JsonHelper::KeyValidateInfo> rootKeyInfoList = {
        { _jsonPlannedHomePositionKey,      QJsonValue::Object, true },
        { _jsonItemsKey,                    QJsonValue::Array,  true },
        { _jsonMavAutopilotKey,             QJsonValue::Double, true },
        { _jsonComplexItemsKey,             QJsonValue::Array,  true },
    };
    if (!JsonHelper::validateKeys(json, rootKeyInfoList, errorString)) {
        return false;
    }

    // Read complex items
    QList<SurveyMissionItem*> surveyItems;
    QJsonArray complexArray(json[_jsonComplexItemsKey].toArray());
    qCDebug(MissionControllerLog) << "Json load: complex item count" << complexArray.count();
    for (int i=0; i<complexArray.count(); i++) {
        const QJsonValue& itemValue = complexArray[i];

        if (!itemValue.isObject()) {
            errorString = QStringLiteral("Mission item is not an object");
            return false;
        }

        SurveyMissionItem* item = new SurveyMissionItem(_controllerVehicle, visualItems);
        const QJsonObject itemObject = itemValue.toObject();
        if (item->load(itemObject, itemObject["id"].toInt(), errorString)) {
            surveyItems.append(item);
        } else {
            return false;
        }
    }

    // Read simple items, interspersing complex items into the full list

    int nextSimpleItemIndex= 0;
    int nextComplexItemIndex= 0;
    int nextSequenceNumber = 1; // Start with 1 since home is in 0
    QJsonArray itemArray(json[_jsonItemsKey].toArray());

    qCDebug(MissionControllerLog) << "Json load: simple item loop start simpleItemCount:ComplexItemCount" << itemArray.count() << surveyItems.count();
    do {
        qCDebug(MissionControllerLog) << "Json load: simple item loop nextSimpleItemIndex:nextComplexItemIndex:nextSequenceNumber" << nextSimpleItemIndex << nextComplexItemIndex << nextSequenceNumber;

        // If there is a complex item that should be next in sequence add it in
        if (nextComplexItemIndex < surveyItems.count()) {
            SurveyMissionItem* complexItem = surveyItems[nextComplexItemIndex];

            if (complexItem->sequenceNumber() == nextSequenceNumber) {
                qCDebug(MissionControllerLog) << "Json load: injecting complex item expectedSequence:actualSequence:" << nextSequenceNumber << complexItem->sequenceNumber();
                visualItems->append(complexItem);
                nextSequenceNumber = complexItem->lastSequenceNumber() + 1;
                nextComplexItemIndex++;
                continue;
            }
        }

        // Add the next available simple item
        if (nextSimpleItemIndex < itemArray.count()) {
            const QJsonValue& itemValue = itemArray[nextSimpleItemIndex++];

            if (!itemValue.isObject()) {
                errorString = QStringLiteral("Mission item is not an object");
                return false;
            }

            const QJsonObject itemObject = itemValue.toObject();
            SimpleMissionItem* item = new SimpleMissionItem(_controllerVehicle, visualItems);
            if (item->load(itemObject, itemObject["id"].toInt(), errorString)) {
                qCDebug(MissionControllerLog) << "Json load: adding simple item expectedSequence:actualSequence" << nextSequenceNumber << item->sequenceNumber();
                nextSequenceNumber = item->lastSequenceNumber() + 1;
                visualItems->append(item);
            } else {
                return false;
            }
        }
    } while (nextSimpleItemIndex < itemArray.count() || nextComplexItemIndex < surveyItems.count());

    if (json.contains(_jsonPlannedHomePositionKey)) {
        SimpleMissionItem* item = new SimpleMissionItem(_controllerVehicle, visualItems);

        if (item->load(json[_jsonPlannedHomePositionKey].toObject(), 0, errorString)) {
            MissionSettingsItem* settingsItem = new MissionSettingsItem(_controllerVehicle, visualItems);
            settingsItem->setCoordinate(item->coordinate());
            visualItems->insert(0, settingsItem);
            item->deleteLater();
        } else {
            return false;
        }
    } else {
        _addMissionSettings(visualItems, true /* addToCenter */);
    }

    return true;
}

bool MissionController::_loadJsonMissionFileV2(const QJsonObject& json, QmlObjectListModel* visualItems, QString& errorString)
{
    // Validate root object keys
    QList<JsonHelper::KeyValidateInfo> rootKeyInfoList = {
        { _jsonPlannedHomePositionKey,      QJsonValue::Array,  true },
        { _jsonItemsKey,                    QJsonValue::Array,  true },
        { _jsonFirmwareTypeKey,             QJsonValue::Double, true },
        { _jsonVehicleTypeKey,              QJsonValue::Double, false },
        { _jsonCruiseSpeedKey,              QJsonValue::Double, false },
        { _jsonHoverSpeedKey,               QJsonValue::Double, false },
    };
    if (!JsonHelper::validateKeys(json, rootKeyInfoList, errorString)) {
        return false;
    }

    qCDebug(MissionControllerLog) << "MissionController::_loadJsonMissionFileV2 itemCount:" << json[_jsonItemsKey].toArray().count();

    // Mission Settings
    AppSettings* appSettings = qgcApp()->toolbox()->settingsManager()->appSettings();

    if (_masterController->offline()) {
        // We only update if offline since if we are online we use the online vehicle settings
        appSettings->offlineEditingFirmwareType()->setRawValue(AppSettings::offlineEditingFirmwareTypeFromFirmwareType((MAV_AUTOPILOT)json[_jsonFirmwareTypeKey].toInt()));
        if (json.contains(_jsonVehicleTypeKey)) {
            appSettings->offlineEditingVehicleType()->setRawValue(AppSettings::offlineEditingVehicleTypeFromVehicleType((MAV_TYPE)json[_jsonVehicleTypeKey].toInt()));
        }
    }
    if (json.contains(_jsonCruiseSpeedKey)) {
        appSettings->offlineEditingCruiseSpeed()->setRawValue(json[_jsonCruiseSpeedKey].toDouble());
    }
    if (json.contains(_jsonHoverSpeedKey)) {
        appSettings->offlineEditingHoverSpeed()->setRawValue(json[_jsonHoverSpeedKey].toDouble());
    }

    QGeoCoordinate homeCoordinate;
    if (!JsonHelper::loadGeoCoordinate(json[_jsonPlannedHomePositionKey], true /* altitudeRequired */, homeCoordinate, errorString)) {
        return false;
    }
    MissionSettingsItem* settingsItem = new MissionSettingsItem(_controllerVehicle, visualItems);
    settingsItem->setCoordinate(homeCoordinate);
    visualItems->insert(0, settingsItem);
    qCDebug(MissionControllerLog) << "plannedHomePosition" << homeCoordinate;

    // Read mission items

    int nextSequenceNumber = 1; // Start with 1 since home is in 0
    const QJsonArray rgMissionItems(json[_jsonItemsKey].toArray());
    for (int i=0; i<rgMissionItems.count(); i++) {
        // Convert to QJsonObject
        const QJsonValue& itemValue = rgMissionItems[i];
        if (!itemValue.isObject()) {
            errorString = tr("Mission item %1 is not an object").arg(i);
            return false;
        }
        const QJsonObject itemObject = itemValue.toObject();

        // Load item based on type

        QList<JsonHelper::KeyValidateInfo> itemKeyInfoList = {
            { VisualMissionItem::jsonTypeKey,  QJsonValue::String, true },
        };
        if (!JsonHelper::validateKeys(itemObject, itemKeyInfoList, errorString)) {
            return false;
        }
        QString itemType = itemObject[VisualMissionItem::jsonTypeKey].toString();

        if (itemType == VisualMissionItem::jsonTypeSimpleItemValue) {
            SimpleMissionItem* simpleItem = new SimpleMissionItem(_controllerVehicle, visualItems);
            if (simpleItem->load(itemObject, nextSequenceNumber, errorString)) {
                qCDebug(MissionControllerLog) << "Loading simple item: nextSequenceNumber:command" << nextSequenceNumber << simpleItem->command();
                nextSequenceNumber = simpleItem->lastSequenceNumber() + 1;
                visualItems->append(simpleItem);
            } else {
                return false;
            }
        } else if (itemType == VisualMissionItem::jsonTypeComplexItemValue) {
            QList<JsonHelper::KeyValidateInfo> complexItemKeyInfoList = {
                { ComplexMissionItem::jsonComplexItemTypeKey,  QJsonValue::String, true },
            };
            if (!JsonHelper::validateKeys(itemObject, complexItemKeyInfoList, errorString)) {
                return false;
            }
            QString complexItemType = itemObject[ComplexMissionItem::jsonComplexItemTypeKey].toString();

            if (complexItemType == SurveyMissionItem::jsonComplexItemTypeValue) {
                qCDebug(MissionControllerLog) << "Loading Survey: nextSequenceNumber" << nextSequenceNumber;
                SurveyMissionItem* surveyItem = new SurveyMissionItem(_controllerVehicle, visualItems);
                if (!surveyItem->load(itemObject, nextSequenceNumber++, errorString)) {
                    return false;
                }
                nextSequenceNumber = surveyItem->lastSequenceNumber() + 1;
                qCDebug(MissionControllerLog) << "Survey load complete: nextSequenceNumber" << nextSequenceNumber;
                visualItems->append(surveyItem);
            } else if (complexItemType == FixedWingLandingComplexItem::jsonComplexItemTypeValue) {
                qCDebug(MissionControllerLog) << "Loading Fixed Wing Landing Pattern: nextSequenceNumber" << nextSequenceNumber;
                FixedWingLandingComplexItem* landingItem = new FixedWingLandingComplexItem(_controllerVehicle, visualItems);
                if (!landingItem->load(itemObject, nextSequenceNumber++, errorString)) {
                    return false;
                }
                nextSequenceNumber = landingItem->lastSequenceNumber() + 1;
                qCDebug(MissionControllerLog) << "FW Landing Pattern load complete: nextSequenceNumber" << nextSequenceNumber;
                visualItems->append(landingItem);
            } else if (complexItemType == StructureScanComplexItem::jsonComplexItemTypeValue) {
                qCDebug(MissionControllerLog) << "Loading Structure Scan: nextSequenceNumber" << nextSequenceNumber;
                StructureScanComplexItem* structureItem = new StructureScanComplexItem(_controllerVehicle, visualItems);
                if (!structureItem->load(itemObject, nextSequenceNumber++, errorString)) {
                    return false;
                }
                nextSequenceNumber = structureItem->lastSequenceNumber() + 1;
                qCDebug(MissionControllerLog) << "Structure Scan load complete: nextSequenceNumber" << nextSequenceNumber;
                visualItems->append(structureItem);
            } else if (complexItemType == MissionSettingsItem::jsonComplexItemTypeValue) {
                qCDebug(MissionControllerLog) << "Loading Mission Settings: nextSequenceNumber" << nextSequenceNumber;
                MissionSettingsItem* settingsItem = new MissionSettingsItem(_controllerVehicle, visualItems);
                if (!settingsItem->load(itemObject, nextSequenceNumber++, errorString)) {
                    return false;
                }
                nextSequenceNumber = settingsItem->lastSequenceNumber() + 1;
                qCDebug(MissionControllerLog) << "Mission Settings load complete: nextSequenceNumber" << nextSequenceNumber;
                visualItems->append(settingsItem);
            } else {
                errorString = tr("Unsupported complex item type: %1").arg(complexItemType);
            }
        } else {
            errorString = tr("Unknown item type: %1").arg(itemType);
            return false;
        }
    }

    // Fix up the DO_JUMP commands jump sequence number by finding the item with the matching doJumpId
    for (int i=0; i<visualItems->count(); i++) {
        if (visualItems->value<VisualMissionItem*>(i)->isSimpleItem()) {
            SimpleMissionItem* doJumpItem = visualItems->value<SimpleMissionItem*>(i);
            if ((MAV_CMD)doJumpItem->command() == MAV_CMD_DO_JUMP) {
                bool found = false;
                int findDoJumpId = doJumpItem->missionItem().param1();
                for (int j=0; j<visualItems->count(); j++) {
                    if (visualItems->value<VisualMissionItem*>(j)->isSimpleItem()) {
                        SimpleMissionItem* targetItem = visualItems->value<SimpleMissionItem*>(j);
                        if (targetItem->missionItem().doJumpId() == findDoJumpId) {
                            doJumpItem->missionItem().setParam1(targetItem->sequenceNumber());
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    errorString = tr("Could not find doJumpId: %1").arg(findDoJumpId);
                    return false;
                }
            }
        }
    }

    return true;
}

bool MissionController::_loadItemsFromJson(const QJsonObject& json, QmlObjectListModel* visualItems, QString& errorString)
{
    // V1 file format has no file type key and version key is string. Convert to new format.
    if (!json.contains(JsonHelper::jsonFileTypeKey)) {
        json[JsonHelper::jsonFileTypeKey] = _jsonFileTypeValue;
    }

    int fileVersion;
    JsonHelper::validateQGCJsonFile(json,
                                    _jsonFileTypeValue,    // expected file type
                                    1,                     // minimum supported version
                                    2,                     // maximum supported version
                                    fileVersion,
                                    errorString);

    if (fileVersion == 1) {
        return _loadJsonMissionFileV1(json, visualItems, errorString);
    } else {
        return _loadJsonMissionFileV2(json, visualItems, errorString);
    }
}

bool MissionController::_loadTextMissionFile(QTextStream& stream, QmlObjectListModel* visualItems, QString& errorString)
{
    bool firstItem = true;
    bool plannedHomePositionInFile = false;

    QString firstLine = stream.readLine();
    const QStringList& version = firstLine.split(" ");

    bool versionOk = false;
    if (version.size() == 3 && version[0] == "QGC" && version[1] == "WPL") {
        if (version[2] == "110") {
            // ArduPilot file, planned home position is already in position 0
            versionOk = true;
            plannedHomePositionInFile = true;
        } else if (version[2] == "120") {
            // Old QGC file, no planned home position
            versionOk = true;
            plannedHomePositionInFile = false;
        }
    }

    if (versionOk) {
        // Start with planned home in center
        _addMissionSettings(visualItems, true /* addToCenter */);
        MissionSettingsItem* settingsItem = visualItems->value<MissionSettingsItem*>(0);

        while (!stream.atEnd()) {
            SimpleMissionItem* item = new SimpleMissionItem(_controllerVehicle, visualItems);

            if (item->load(stream)) {
                if (firstItem && plannedHomePositionInFile) {
                    settingsItem->setCoordinate(item->coordinate());
                } else {
                    visualItems->append(item);
                }
                firstItem = false;
            } else {
                errorString = tr("The mission file is corrupted.");
                return false;
            }
        }
    } else {
        errorString = tr("The mission file is not compatible with this version of %1.").arg(qgcApp()->applicationName());
        return false;
    }

    if (!plannedHomePositionInFile) {
        // Update sequence numbers in DO_JUMP commands to take into account added home position in index 0
        for (int i=1; i<visualItems->count(); i++) {
            SimpleMissionItem* item = qobject_cast<SimpleMissionItem*>(visualItems->get(i));
            if (item && item->command() == MavlinkQmlSingleton::MAV_CMD_DO_JUMP) {
                item->missionItem().setParam1((int)item->missionItem().param1() + 1);
            }
        }
    }

    return true;
}

void MissionController::_initLoadedVisualItems(QmlObjectListModel* loadedVisualItems)
{
    if (_visualItems) {
        _deinitAllVisualItems();
        _visualItems->deleteLater();
        _settingsItem = NULL;
    }

    _visualItems = loadedVisualItems;

    if (_visualItems->count() == 0) {
        _addMissionSettings(_visualItems, true /* addToCenter */);
    }

    MissionController::_scanForAdditionalSettings(_visualItems, _controllerVehicle);

    _initAllVisualItems();
}

bool MissionController::load(const QJsonObject& json, QString& errorString)
{
    QString errorStr;
    QString errorMessage = tr("Mission: %1");
    QmlObjectListModel* loadedVisualItems = new QmlObjectListModel(this);

    if (!_loadJsonMissionFileV2(json, loadedVisualItems, errorStr)) {
        errorString = errorMessage.arg(errorStr);
        return false;
    }
    _initLoadedVisualItems(loadedVisualItems);

    return true;
}

bool MissionController::loadJsonFile(QFile& file, QString& errorString)
{
    QString         errorStr;
    QString         errorMessage = tr("Mission: %1");
    QJsonDocument   jsonDoc;
    QByteArray      bytes = file.readAll();

    if (!JsonHelper::isJsonFile(bytes, jsonDoc, errorStr)) {
        errorString = errorMessage.arg(errorStr);
        return false;
    }

    QJsonObject json = jsonDoc.object();
    QmlObjectListModel* loadedVisualItems = new QmlObjectListModel(this);
    if (!_loadItemsFromJson(json, loadedVisualItems, errorStr)) {
        errorString = errorMessage.arg(errorStr);
        return false;
    }

    _initLoadedVisualItems(loadedVisualItems);

    return true;
}

bool MissionController::loadTextFile(QFile& file, QString& errorString)
{
    QString     errorStr;
    QString     errorMessage = tr("Mission: %1");
    QByteArray  bytes = file.readAll();
    QTextStream stream(bytes);

    QmlObjectListModel* loadedVisualItems = new QmlObjectListModel(this);
    if (!_loadTextMissionFile(stream, loadedVisualItems, errorStr)) {
        errorString = errorMessage.arg(errorStr);
        return false;
    }

    _initLoadedVisualItems(loadedVisualItems);

    return true;
}

void MissionController::save(QJsonObject& json)
{
    json[JsonHelper::jsonVersionKey] = _missionFileVersion;

    // Mission settings

    MissionSettingsItem* settingsItem = _visualItems->value<MissionSettingsItem*>(0);
    if (!settingsItem) {
        qWarning() << "First item is not MissionSettingsItem";
        return;
    }
    QJsonValue coordinateValue;
    JsonHelper::saveGeoCoordinate(settingsItem->coordinate(), true /* writeAltitude */, coordinateValue);
    json[_jsonPlannedHomePositionKey] = coordinateValue;
    json[_jsonFirmwareTypeKey] = _controllerVehicle->firmwareType();
    json[_jsonVehicleTypeKey] = _controllerVehicle->vehicleType();
    json[_jsonCruiseSpeedKey] = _controllerVehicle->defaultCruiseSpeed();
    json[_jsonHoverSpeedKey] = _controllerVehicle->defaultHoverSpeed();

    // Save the visual items

    QJsonArray rgJsonMissionItems;
    for (int i=0; i<_visualItems->count(); i++) {
        VisualMissionItem* visualItem = qobject_cast<VisualMissionItem*>(_visualItems->get(i));

        visualItem->save(rgJsonMissionItems);
    }

    // Mission settings has a special case for end mission action
    if (settingsItem) {
        QList<MissionItem*> rgMissionItems;

        if (_convertToMissionItems(_visualItems, rgMissionItems, this /* missionItemParent */)) {
            QJsonObject saveObject;
            MissionItem* missionItem = rgMissionItems[rgMissionItems.count() - 1];
            missionItem->save(saveObject);
            rgJsonMissionItems.append(saveObject);
        }
        for (int i=0; i<rgMissionItems.count(); i++) {
            rgMissionItems[i]->deleteLater();
        }
    }

    json[_jsonItemsKey] = rgJsonMissionItems;
}

void MissionController::_calcPrevWaypointValues(double homeAlt, VisualMissionItem* currentItem, VisualMissionItem* prevItem, double* azimuth, double* distance, double* altDifference)
{
    QGeoCoordinate  currentCoord =  currentItem->coordinate();
    QGeoCoordinate  prevCoord =     prevItem->exitCoordinate();
    bool            distanceOk =    false;

    // Convert to fixed altitudes

    distanceOk = true;
    if (currentItem != _settingsItem && currentItem->coordinateHasRelativeAltitude()) {
        currentCoord.setAltitude(homeAlt + currentCoord.altitude());
    }
    if (prevItem != _settingsItem && prevItem->exitCoordinateHasRelativeAltitude()) {
        prevCoord.setAltitude(homeAlt + prevCoord.altitude());
    }

    if (distanceOk) {
        *altDifference = currentCoord.altitude() - prevCoord.altitude();
        *distance = prevCoord.distanceTo(currentCoord);
        *azimuth = prevCoord.azimuthTo(currentCoord);
    } else {
        *altDifference = 0.0;
        *azimuth = 0.0;
        *distance = 0.0;
    }
}

double MissionController::_calcDistanceToHome(VisualMissionItem* currentItem, VisualMissionItem* homeItem)
{
    QGeoCoordinate  currentCoord =  currentItem->coordinate();
    QGeoCoordinate  homeCoord =     homeItem->exitCoordinate();
    bool            distanceOk =    false;

    distanceOk = true;

    return distanceOk ? homeCoord.distanceTo(currentCoord) : 0.0;
}

void MissionController::_addWaypointLineSegment(CoordVectHashTable& prevItemPairHashTable, VisualItemPair& pair)
{
    if (prevItemPairHashTable.contains(pair)) {
        // Pair already exists and connected, just re-use
        _linesTable[pair] = prevItemPairHashTable.take(pair);
    } else {
        // Create a new segment and wire update notifiers
        auto linevect       = new CoordinateVector(pair.first->isSimpleItem() ? pair.first->coordinate() : pair.first->exitCoordinate(), pair.second->coordinate(), this);
        auto originNotifier = pair.first->isSimpleItem() ? &VisualMissionItem::coordinateChanged : &VisualMissionItem::exitCoordinateChanged;
        auto endNotifier    = &VisualMissionItem::coordinateChanged;

        // Use signals/slots to update the coordinate endpoints
        connect(pair.first,     originNotifier, linevect, &CoordinateVector::setCoordinate1);
        connect(pair.second,    endNotifier,    linevect, &CoordinateVector::setCoordinate2);

        // FIXME: We should ideally have signals for 2D position change, alt change, and 3D position change
        // Not optimal, but still pretty fast, do a full update of range/bearing/altitudes
        connect(pair.second, &VisualMissionItem::coordinateChanged, this, &MissionController::_recalcMissionFlightStatus);
        _linesTable[pair] = linevect;
    }
}

void MissionController::_recalcWaypointLines(void)
{
    bool                firstCoordinateItem =   true;
    VisualMissionItem*  lastCoordinateItem =    qobject_cast<VisualMissionItem*>(_visualItems->get(0));

    bool homePositionValid = _settingsItem->coordinate().isValid();

    qCDebug(MissionControllerLog) << "_recalcWaypointLines homePositionValid" << homePositionValid;

    CoordVectHashTable old_table = _linesTable;
    _linesTable.clear();
    _waypointLines.clear();
    _waypointPath.clear();

    bool linkEndToHome;
    SimpleMissionItem* lastItem = _visualItems->value<SimpleMissionItem*>(_visualItems->count() - 1);
    if (lastItem && (int)lastItem->command() == MAV_CMD_NAV_RETURN_TO_LAUNCH) {
        linkEndToHome = true;
    } else {
        linkEndToHome = _settingsItem->missionEndRTL();
    }

    bool linkStartToHome = false;
    for (int i=1; i<_visualItems->count(); i++) {
        VisualMissionItem* item = qobject_cast<VisualMissionItem*>(_visualItems->get(i));

        // If we still haven't found the first coordinate item and we hit a takeoff command this means the mission starts from the ground.
        // Link the first item back to home to show that.
        if (firstCoordinateItem && item->isSimpleItem()) {
            MAV_CMD command = (MAV_CMD)qobject_cast<SimpleMissionItem*>(item)->command();
            if (command == MAV_CMD_NAV_TAKEOFF || command == MAV_CMD_NAV_VTOL_TAKEOFF) {
                linkStartToHome = true;
            }
        }

        if (item->specifiesCoordinate() && !item->isStandaloneCoordinate()) {
            firstCoordinateItem = false;
            if (lastCoordinateItem != _settingsItem || (homePositionValid && linkStartToHome)) {
                if (_editMode) {
                    VisualItemPair pair(lastCoordinateItem, item);
                    _addWaypointLineSegment(old_table, pair);
                }
            }
            _waypointPath.append(QVariant::fromValue(item->coordinate()));
            lastCoordinateItem = item;
        }
    }

    if (linkStartToHome && homePositionValid) {
        _waypointPath.prepend(QVariant::fromValue(_settingsItem->coordinate()));
    }

    if (linkEndToHome && lastCoordinateItem != _settingsItem && homePositionValid) {
        if (_editMode) {
            VisualItemPair pair(lastCoordinateItem, _settingsItem);
            _addWaypointLineSegment(old_table, pair);
        } else {
            _waypointPath.append(QVariant::fromValue(_settingsItem->coordinate()));
        }
    }

    {
        // Create a temporary QObjectList and replace the model data
        QObjectList objs;
        objs.reserve(_linesTable.count());
        foreach(CoordinateVector *vect, _linesTable.values()) {
            objs.append(vect);
        }

        // We don't delete here because many links may still be valid
        _waypointLines.swapObjectList(objs);
    }

    // Anything left in the old table is an obsolete line object that can go
    qDeleteAll(old_table);

    _recalcMissionFlightStatus();

    if (_waypointPath.count() == 0) {
        // MapPolyLine has a bug where if you can from a path which has elements to an empty path the line drawn
        // is not cleared from the map. This hack works around that since it causes the previous lines to be remove
        // as then doesn't draw anything on the map.
        _waypointPath.append(QVariant::fromValue(QGeoCoordinate(0, 0)));
        _waypointPath.append(QVariant::fromValue(QGeoCoordinate(0, 0)));
    }

    emit waypointLinesChanged();
    emit waypointPathChanged();
}

void MissionController::_updateBatteryInfo(int waypointIndex)
{
    if (_missionFlightStatus.mAhBattery != 0) {
        _missionFlightStatus.hoverAmpsTotal = (_missionFlightStatus.hoverTime / 60.0) * _missionFlightStatus.hoverAmps;
        _missionFlightStatus.cruiseAmpsTotal = (_missionFlightStatus.cruiseTime / 60.0) * _missionFlightStatus.cruiseAmps;
        _missionFlightStatus.batteriesRequired = ceil((_missionFlightStatus.hoverAmpsTotal + _missionFlightStatus.cruiseAmpsTotal) / _missionFlightStatus.ampMinutesAvailable);
        // FIXME: Battery change point code pretty much doesn't work. The reason is that is treats complex items as a black box. It needs to be able to look
        // inside complex items in order to determine a swap point that is interior to a complex item. Current the swap point display in PlanToolbar is
        // disabled to do this problem.
        if (waypointIndex != -1 && _missionFlightStatus.batteriesRequired == 2 && _missionFlightStatus.batteryChangePoint == -1) {
            _missionFlightStatus.batteryChangePoint = waypointIndex - 1;
        }
    }
}

void MissionController::_addHoverTime(double hoverTime, double hoverDistance, int waypointIndex)
{
    _missionFlightStatus.totalTime += hoverTime;
    _missionFlightStatus.hoverTime += hoverTime;
    _missionFlightStatus.hoverDistance += hoverDistance;
    _missionFlightStatus.totalDistance += hoverDistance;
    _updateBatteryInfo(waypointIndex);
}

void MissionController::_addCruiseTime(double cruiseTime, double cruiseDistance, int waypointIndex)
{
    _missionFlightStatus.totalTime += cruiseTime;
    _missionFlightStatus.cruiseTime += cruiseTime;
    _missionFlightStatus.cruiseDistance += cruiseDistance;
    _missionFlightStatus.totalDistance += cruiseDistance;
    _updateBatteryInfo(waypointIndex);
}

/// Adds additional time to a mission as specified by the command
void MissionController::_addCommandTimeDelay(SimpleMissionItem* simpleItem, bool vtolInHover)
{
    double seconds = 0;

    if (!simpleItem) {
        return;
    }

    // This routine is currently quite minimal and only handles the simple cases.
    switch ((int)simpleItem->command()) {
    case MAV_CMD_NAV_WAYPOINT:
    case MAV_CMD_CONDITION_DELAY:
        seconds = simpleItem->missionItem().param1();
        break;
    }

    _addTimeDistance(vtolInHover, 0, 0, seconds, 0, -1);
}

/// Adds the specified time to the appropriate hover or cruise time values.
///     @param vtolInHover true: vtol is currrent in hover mode
///     @param hoverTime    Amount of time tp add to hover
///     @param cruiseTime   Amount of time to add to cruise
///     @param extraTime    Amount of additional time to add to hover/cruise
///     @param seqNum       Sequence number of waypoint for these values, -1 for no waypoint associated
void MissionController::_addTimeDistance(bool vtolInHover, double hoverTime, double cruiseTime, double extraTime, double distance, int seqNum)
{
    if (_controllerVehicle->vtol()) {
        if (vtolInHover) {
            _addHoverTime(hoverTime, distance, seqNum);
            _addHoverTime(extraTime, 0, -1);
        } else {
            _addCruiseTime(cruiseTime, distance, seqNum);
            _addCruiseTime(extraTime, 0, -1);
        }
    } else {
        if (_controllerVehicle->multiRotor()) {
            _addHoverTime(hoverTime, distance, seqNum);
            _addHoverTime(extraTime, 0, -1);
        } else {
            _addCruiseTime(cruiseTime, distance, seqNum);
            _addCruiseTime(extraTime, 0, -1);
        }
    }
}

void MissionController::_recalcMissionFlightStatus()
{
    if (!_visualItems->count()) {
        return;
    }

    bool                firstCoordinateItem =   true;
    VisualMissionItem*  lastCoordinateItem =    qobject_cast<VisualMissionItem*>(_visualItems->get(0));

    bool showHomePosition = _settingsItem->coordinate().isValid();

    qCDebug(MissionControllerLog) << "_recalcMissionFlightStatus";

    // If home position is valid we can calculate distances between all waypoints.
    // If home position is not valid we can only calculate distances between waypoints which are
    // both relative altitude.

    // No values for first item
    lastCoordinateItem->setAltDifference(0.0);
    lastCoordinateItem->setAzimuth(0.0);
    lastCoordinateItem->setDistance(0.0);

    double minAltSeen = 0.0;
    double maxAltSeen = 0.0;
    const double homePositionAltitude = _settingsItem->coordinate().altitude();
    minAltSeen = maxAltSeen = _settingsItem->coordinate().altitude();

    _resetMissionFlightStatus();

    bool vtolInHover = true;
    bool linkStartToHome = false;
    bool linkEndToHome = false;

    if (showHomePosition) {
        SimpleMissionItem* lastItem = _visualItems->value<SimpleMissionItem*>(_visualItems->count() - 1);
        if (lastItem && (int)lastItem->command() == MAV_CMD_NAV_RETURN_TO_LAUNCH) {
            linkEndToHome = true;
        } else {
            linkEndToHome = _settingsItem->missionEndRTL();
        }
    }

    for (int i=0; i<_visualItems->count(); i++) {
        VisualMissionItem* item = qobject_cast<VisualMissionItem*>(_visualItems->get(i));
        SimpleMissionItem* simpleItem = qobject_cast<SimpleMissionItem*>(item);
        ComplexMissionItem* complexItem = qobject_cast<ComplexMissionItem*>(item);

        // Assume the worst
        item->setAzimuth(0.0);
        item->setDistance(0.0);

        // Look for speed changed
        double newSpeed = item->specifiedFlightSpeed();
        if (!qIsNaN(newSpeed)) {
            if (_controllerVehicle->multiRotor()) {
                _missionFlightStatus.hoverSpeed = newSpeed;
            } else if (_controllerVehicle->vtol()) {
                if (vtolInHover) {
                    _missionFlightStatus.hoverSpeed = newSpeed;
                } else {
                    _missionFlightStatus.cruiseSpeed = newSpeed;
                }
            } else {
                _missionFlightStatus.cruiseSpeed = newSpeed;
            }
            _missionFlightStatus.vehicleSpeed = newSpeed;
        }

        // Look for gimbal change
        double gimbalYaw = item->specifiedGimbalYaw();
        if (!qIsNaN(gimbalYaw)) {
            _missionFlightStatus.gimbalYaw = gimbalYaw;
        }
        double gimbalPitch = item->specifiedGimbalPitch();
        if (!qIsNaN(gimbalPitch)) {
            _missionFlightStatus.gimbalPitch = gimbalPitch;
        }

        if (i == 0) {
            // We only process speed and gimbal from Mission Settings item
            continue;
        }

        // Link back to home if first item is takeoff and we have home position
        if (firstCoordinateItem && simpleItem && simpleItem->command() == MavlinkQmlSingleton::MAV_CMD_NAV_TAKEOFF) {
            if (showHomePosition) {
                linkStartToHome = true;
                if (_controllerVehicle->multiRotor() || _controllerVehicle->vtol()) {
                    // We have to special case takeoff, assuming vehicle takes off straight up to specified altitude
                    double azimuth, distance, altDifference;
                    _calcPrevWaypointValues(homePositionAltitude, _settingsItem, simpleItem, &azimuth, &distance, &altDifference);
                    double takeoffTime = qAbs(altDifference) / _appSettings->offlineEditingAscentSpeed()->rawValue().toDouble();
                    _addHoverTime(takeoffTime, 0, -1);
                }
            }
        }

        // Update VTOL state
        if (simpleItem && _controllerVehicle->vtol()) {
            switch (simpleItem->command()) {
            case MavlinkQmlSingleton::MAV_CMD_NAV_TAKEOFF:
                vtolInHover = false;
                break;
            case MavlinkQmlSingleton::MAV_CMD_NAV_LAND:
                vtolInHover = false;
                break;
            case MavlinkQmlSingleton::MAV_CMD_DO_VTOL_TRANSITION:
            {
                int transitionState = simpleItem->missionItem().param1();
                if (transitionState == MAV_VTOL_STATE_TRANSITION_TO_MC) {
                    vtolInHover = true;
                } else if (transitionState == MAV_VTOL_STATE_TRANSITION_TO_FW) {
                    vtolInHover = false;
                }
            }
                break;
            default:
                break;
            }
        }

        // Check for command specific time delays
        _addCommandTimeDelay(simpleItem, vtolInHover);

        if (item->specifiesCoordinate()) {
            // Update vehicle yaw assuming direction to next waypoint
            if (item != lastCoordinateItem) {
                _missionFlightStatus.vehicleYaw = lastCoordinateItem->exitCoordinate().azimuthTo(item->coordinate());
                lastCoordinateItem->setMissionVehicleYaw(_missionFlightStatus.vehicleYaw);
            }

            // Keep track of the min/max altitude for all waypoints so we can show altitudes as a percentage

            double absoluteAltitude = item->coordinate().altitude();
            if (item->coordinateHasRelativeAltitude()) {
                absoluteAltitude += homePositionAltitude;
            }
            minAltSeen = std::min(minAltSeen, absoluteAltitude);
            maxAltSeen = std::max(maxAltSeen, absoluteAltitude);

            double terrainAltitude = item->terrainAltitude();
            if (!qIsNaN(terrainAltitude)) {
                minAltSeen = std::min(minAltSeen, terrainAltitude);
                maxAltSeen = std::max(maxAltSeen, terrainAltitude);
            }

            if (!item->isStandaloneCoordinate()) {
                firstCoordinateItem = false;
                if (lastCoordinateItem != _settingsItem || linkStartToHome) {
                    // This is a subsequent waypoint or we are forcing the first waypoint back to home
                    double azimuth, distance, altDifference;

                    _calcPrevWaypointValues(homePositionAltitude, item, lastCoordinateItem, &azimuth, &distance, &altDifference);
                    item->setAltDifference(altDifference);
                    item->setAzimuth(azimuth);
                    item->setDistance(distance);

                    _missionFlightStatus.maxTelemetryDistance = qMax(_missionFlightStatus.maxTelemetryDistance, _calcDistanceToHome(item, _settingsItem));

                    // Calculate time/distance
                    double hoverTime = distance / _missionFlightStatus.hoverSpeed;
                    double cruiseTime = distance / _missionFlightStatus.cruiseSpeed;
                    _addTimeDistance(vtolInHover, hoverTime, cruiseTime, 0, distance, item->sequenceNumber());
                }

                if (complexItem) {
                    // Add in distance/time inside complex items as well
                    double distance = complexItem->complexDistance();
                    _missionFlightStatus.maxTelemetryDistance = qMax(_missionFlightStatus.maxTelemetryDistance, complexItem->greatestDistanceTo(complexItem->exitCoordinate()));

                    double hoverTime = distance / _missionFlightStatus.hoverSpeed;
                    double cruiseTime = distance / _missionFlightStatus.cruiseSpeed;
                    double extraTime = complexItem->additionalTimeDelay();
                    _addTimeDistance(vtolInHover, hoverTime, cruiseTime, extraTime, distance, item->sequenceNumber());
                }

                item->setMissionFlightStatus(_missionFlightStatus);
            }

            lastCoordinateItem = item;
        }
    }
    lastCoordinateItem->setMissionVehicleYaw(_missionFlightStatus.vehicleYaw);

    if (linkEndToHome && lastCoordinateItem != _settingsItem) {
        double azimuth, distance, altDifference;
        _calcPrevWaypointValues(homePositionAltitude, lastCoordinateItem, _settingsItem, &azimuth, &distance, &altDifference);

        // Calculate time/distance
        double hoverTime = distance / _missionFlightStatus.hoverSpeed;
        double cruiseTime = distance / _missionFlightStatus.cruiseSpeed;
        double landTime = qAbs(altDifference) / _appSettings->offlineEditingDescentSpeed()->rawValue().toDouble();
        _addTimeDistance(vtolInHover, hoverTime, cruiseTime, distance, landTime, -1);
    }

    if (_missionFlightStatus.mAhBattery != 0 && _missionFlightStatus.batteryChangePoint == -1) {
        _missionFlightStatus.batteryChangePoint = 0;
    }

    emit missionMaxTelemetryChanged(_missionFlightStatus.maxTelemetryDistance);
    emit missionDistanceChanged(_missionFlightStatus.totalDistance);
    emit missionHoverDistanceChanged(_missionFlightStatus.hoverDistance);
    emit missionCruiseDistanceChanged(_missionFlightStatus.cruiseDistance);
    emit missionTimeChanged();
    emit missionHoverTimeChanged();
    emit missionCruiseTimeChanged();
    emit batteryChangePointChanged(_missionFlightStatus.batteryChangePoint);
    emit batteriesRequiredChanged(_missionFlightStatus.batteriesRequired);

    // Walk the list again calculating altitude percentages
    double altRange = maxAltSeen - minAltSeen;
    for (int i=0; i<_visualItems->count(); i++) {
        VisualMissionItem* item = qobject_cast<VisualMissionItem*>(_visualItems->get(i));

        if (item->specifiesCoordinate()) {
            double absoluteAltitude = item->coordinate().altitude();
            if (item->coordinateHasRelativeAltitude()) {
                absoluteAltitude += homePositionAltitude;
            }
            if (altRange == 0.0) {
                item->setAltPercent(0.0);
                item->setTerrainPercent(qQNaN());
            } else {
                item->setAltPercent((absoluteAltitude - minAltSeen) / altRange);
                if (!qIsNaN(item->terrainAltitude())) {
                    item->setTerrainPercent((item->terrainAltitude() - minAltSeen) / altRange);
                }
            }
        }
    }
}

// This will update the sequence numbers to be sequential starting from 0
void MissionController::_recalcSequence(void)
{
    // Setup ascending sequence numbers for all visual items

    int sequenceNumber = 0;
    for (int i=0; i<_visualItems->count(); i++) {
        VisualMissionItem* item = qobject_cast<VisualMissionItem*>(_visualItems->get(i));

        item->setSequenceNumber(sequenceNumber);
        sequenceNumber = item->lastSequenceNumber() + 1;
    }
}

// This will update the child item hierarchy
void MissionController::_recalcChildItems(void)
{
    VisualMissionItem* currentParentItem = qobject_cast<VisualMissionItem*>(_visualItems->get(0));

    currentParentItem->childItems()->clear();

    for (int i=1; i<_visualItems->count(); i++) {
        VisualMissionItem* item = qobject_cast<VisualMissionItem*>(_visualItems->get(i));

        // Set up non-coordinate item child hierarchy
        if (item->specifiesCoordinate()) {
            item->childItems()->clear();
            currentParentItem = item;
        } else if (item->isSimpleItem()) {
            currentParentItem->childItems()->append(item);
        }
    }
}

void MissionController::_setPlannedHomePositionFromFirstCoordinate(void)
{
    if (_settingsItem->coordinate().isValid()) {
        return;
    }

    // Set the planned home position to be a delta from first coordinate
    for (int i=1; i<_visualItems->count(); i++) {
        VisualMissionItem* item = _visualItems->value<VisualMissionItem*>(i);

        if (item->specifiesCoordinate()) {
            _settingsItem->setCoordinate(item->coordinate().atDistanceAndAzimuth(30, 0));
        }
    }
}


void MissionController::_recalcAll(void)
{
    if (_editMode) {
        _setPlannedHomePositionFromFirstCoordinate();
    }
    _recalcSequence();
    _recalcChildItems();
    _recalcWaypointLines();
}

/// Initializes a new set of mission items
void MissionController::_initAllVisualItems(void)
{
    // Setup home position at index 0

    _settingsItem = qobject_cast<MissionSettingsItem*>(_visualItems->get(0));
    if (!_settingsItem) {
        qWarning() << "First item not MissionSettingsItem";
        return;
    }
    if (_editMode) {
        _settingsItem->setIsCurrentItem(true);
    }

    if (_managerVehicle->homePosition().isValid()) {
        _settingsItem->setCoordinate(_managerVehicle->homePosition());
    }

    connect(_settingsItem, &MissionSettingsItem::coordinateChanged,     this, &MissionController::_recalcAll);
    connect(_settingsItem, &MissionSettingsItem::missionEndRTLChanged,  this, &MissionController::_recalcAll);
    connect(_settingsItem, &MissionSettingsItem::coordinateChanged,     this, &MissionController::plannedHomePositionChanged);

    for (int i=0; i<_visualItems->count(); i++) {
        VisualMissionItem* item = qobject_cast<VisualMissionItem*>(_visualItems->get(i));
        _initVisualItem(item);
    }

    _recalcAll();

    connect(_visualItems, &QmlObjectListModel::dirtyChanged, this, &MissionController::_visualItemsDirtyChanged);
    connect(_visualItems, &QmlObjectListModel::countChanged, this, &MissionController::_updateContainsItems);

    emit visualItemsChanged();
    emit containsItemsChanged(containsItems());
    emit plannedHomePositionChanged(plannedHomePosition());

    setDirty(false);
}

void MissionController::_deinitAllVisualItems(void)
{
    disconnect(_settingsItem, &MissionSettingsItem::coordinateChanged, this, &MissionController::_recalcAll);
    disconnect(_settingsItem, &MissionSettingsItem::coordinateChanged, this, &MissionController::plannedHomePositionChanged);

    for (int i=0; i<_visualItems->count(); i++) {
        _deinitVisualItem(qobject_cast<VisualMissionItem*>(_visualItems->get(i)));
    }

    disconnect(_visualItems, &QmlObjectListModel::dirtyChanged, this, &MissionController::dirtyChanged);
    disconnect(_visualItems, &QmlObjectListModel::countChanged, this, &MissionController::_updateContainsItems);
}

void MissionController::_initVisualItem(VisualMissionItem* visualItem)
{
    setDirty(false);

    connect(visualItem, &VisualMissionItem::specifiesCoordinateChanged,                 this, &MissionController::_recalcWaypointLines);
    connect(visualItem, &VisualMissionItem::coordinateHasRelativeAltitudeChanged,       this, &MissionController::_recalcWaypointLines);
    connect(visualItem, &VisualMissionItem::exitCoordinateHasRelativeAltitudeChanged,   this, &MissionController::_recalcWaypointLines);
    connect(visualItem, &VisualMissionItem::specifiedFlightSpeedChanged,                this, &MissionController::_recalcMissionFlightStatus);
    connect(visualItem, &VisualMissionItem::specifiedGimbalYawChanged,                  this, &MissionController::_recalcMissionFlightStatus);
    connect(visualItem, &VisualMissionItem::specifiedGimbalPitchChanged,                this, &MissionController::_recalcMissionFlightStatus);
    connect(visualItem, &VisualMissionItem::terrainAltitudeChanged,                     this, &MissionController::_recalcMissionFlightStatus);
    connect(visualItem, &VisualMissionItem::lastSequenceNumberChanged,                  this, &MissionController::_recalcSequence);

    if (visualItem->isSimpleItem()) {
        // We need to track commandChanged on simple item since recalc has special handling for takeoff command
        SimpleMissionItem* simpleItem = qobject_cast<SimpleMissionItem*>(visualItem);
        if (simpleItem) {
            connect(&simpleItem->missionItem()._commandFact, &Fact::valueChanged, this, &MissionController::_itemCommandChanged);
        } else {
            qWarning() << "isSimpleItem == true, yet not SimpleMissionItem";
        }
    } else {
        ComplexMissionItem* complexItem = qobject_cast<ComplexMissionItem*>(visualItem);
        if (complexItem) {
            connect(complexItem, &ComplexMissionItem::complexDistanceChanged,       this, &MissionController::_recalcMissionFlightStatus);
            connect(complexItem, &ComplexMissionItem::greatestDistanceToChanged,    this, &MissionController::_recalcMissionFlightStatus);
            connect(complexItem, &ComplexMissionItem::additionalTimeDelayChanged,   this, &MissionController::_recalcMissionFlightStatus);
        } else {
            qWarning() << "ComplexMissionItem not found";
        }
    }
}

void MissionController::_deinitVisualItem(VisualMissionItem* visualItem)
{
    // Disconnect all signals
    disconnect(visualItem, 0, 0, 0);
}

void MissionController::_itemCommandChanged(void)
{
    _recalcChildItems();
    _recalcWaypointLines();
}

void MissionController::managerVehicleChanged(Vehicle* managerVehicle)
{
    if (_managerVehicle) {
        _missionManager->disconnect(this);
        _managerVehicle->disconnect(this);
        _managerVehicle = NULL;
        _missionManager = NULL;
    }

    _managerVehicle = managerVehicle;
    if (!_managerVehicle) {
        qWarning() << "MissionController::managerVehicleChanged managerVehicle=NULL";
        return;
    }

    _missionManager = _managerVehicle->missionManager();
    connect(_missionManager, &MissionManager::newMissionItemsAvailable, this, &MissionController::_newMissionItemsAvailableFromVehicle);
    connect(_missionManager, &MissionManager::sendComplete,             this, &MissionController::_managerSendComplete);
    connect(_missionManager, &MissionManager::removeAllComplete,        this, &MissionController::_managerRemoveAllComplete);
    connect(_missionManager, &MissionManager::inProgressChanged,        this, &MissionController::_inProgressChanged);
    connect(_missionManager, &MissionManager::progressPct,              this, &MissionController::_progressPctChanged);
    connect(_missionManager, &MissionManager::currentIndexChanged,      this, &MissionController::_currentMissionIndexChanged);
    connect(_missionManager, &MissionManager::lastCurrentIndexChanged,  this, &MissionController::resumeMissionIndexChanged);
    connect(_missionManager, &MissionManager::resumeMissionReady,       this, &MissionController::resumeMissionReady);
    connect(_missionManager, &MissionManager::resumeMissionUploadFail,  this, &MissionController::resumeMissionUploadFail);
    connect(_managerVehicle, &Vehicle::homePositionChanged,             this, &MissionController::_managerVehicleHomePositionChanged);
    connect(_managerVehicle, &Vehicle::defaultCruiseSpeedChanged,       this, &MissionController::_recalcMissionFlightStatus);
    connect(_managerVehicle, &Vehicle::defaultHoverSpeedChanged,        this, &MissionController::_recalcMissionFlightStatus);
    connect(_managerVehicle, &Vehicle::vehicleTypeChanged,              this, &MissionController::complexMissionItemNamesChanged);

    if (!_masterController->offline()) {
        _managerVehicleHomePositionChanged(_managerVehicle->homePosition());
    }

    emit complexMissionItemNamesChanged();
    emit resumeMissionIndexChanged();
}

void MissionController::_managerVehicleHomePositionChanged(const QGeoCoordinate& homePosition)
{
    if (_visualItems) {
        MissionSettingsItem* settingsItem = qobject_cast<MissionSettingsItem*>(_visualItems->get(0));
        if (settingsItem) {
            settingsItem->setHomePositionFromVehicle(homePosition);
        } else {
            qWarning() << "First item is not MissionSettingsItem";
        }
        // Don't let this trip the dirty bit. Otherwise plan will keep getting marked dirty if vehicle home
        // changes.
        _visualItems->setDirty(false);
    }
}

void MissionController::_inProgressChanged(bool inProgress)
{
    emit syncInProgressChanged(inProgress);
}

bool MissionController::_findPreviousAltitude(int newIndex, double* prevAltitude, MAV_FRAME* prevFrame)
{
    bool        found = false;
    double      foundAltitude;
    MAV_FRAME   foundFrame;

    if (newIndex > _visualItems->count()) {
        return false;
    }
    newIndex--;

    for (int i=newIndex; i>0; i--) {
        VisualMissionItem* visualItem = qobject_cast<VisualMissionItem*>(_visualItems->get(i));

        if (visualItem->specifiesCoordinate() && !visualItem->isStandaloneCoordinate()) {
            if (visualItem->isSimpleItem()) {
                SimpleMissionItem* simpleItem = qobject_cast<SimpleMissionItem*>(visualItem);
                if ((MAV_CMD)simpleItem->command() == MAV_CMD_NAV_WAYPOINT) {
                    foundAltitude = simpleItem->exitCoordinate().altitude();
                    foundFrame = simpleItem->missionItem().frame();
                    found = true;
                    break;
                }
            }
        }
    }

    if (found) {
        *prevAltitude = foundAltitude;
        *prevFrame = foundFrame;
    }

    return found;
}

double MissionController::_normalizeLat(double lat)
{
    // Normalize latitude to range: 0 to 180, S to N
    return lat + 90.0;
}

double MissionController::_normalizeLon(double lon)
{
    // Normalize longitude to range: 0 to 360, W to E
    return lon  + 180.0;
}

/// Add the Mission Settings complex item to the front of the items
void MissionController::_addMissionSettings(QmlObjectListModel* visualItems, bool addToCenter)
{
    MissionSettingsItem* settingsItem = new MissionSettingsItem(_controllerVehicle, visualItems);

    qCDebug(MissionControllerLog) << "_addMissionSettings addToCenter" << addToCenter;

    visualItems->insert(0, settingsItem);

    if (addToCenter) {
        if (visualItems->count() > 1) {
            double north = 0.0;
            double south = 0.0;
            double east  = 0.0;
            double west  = 0.0;
            bool firstCoordSet = false;

            for (int i=1; i<visualItems->count(); i++) {
                VisualMissionItem* item = qobject_cast<VisualMissionItem*>(visualItems->get(i));
                if (item->specifiesCoordinate()) {
                    if (firstCoordSet) {
                        double lat = _normalizeLat(item->coordinate().latitude());
                        double lon = _normalizeLon(item->coordinate().longitude());
                        north = fmax(north, lat);
                        south = fmin(south, lat);
                        east  = fmax(east, lon);
                        west  = fmin(west, lon);
                    } else {
                        firstCoordSet = true;
                        north = _normalizeLat(item->coordinate().latitude());
                        south = north;
                        east  = _normalizeLon(item->coordinate().longitude());
                        west  = east;
                    }
                }
            }

            if (firstCoordSet) {
                settingsItem->setCoordinate(QGeoCoordinate((south + ((north - south) / 2)) - 90.0, (west + ((east - west) / 2)) - 180.0, 0.0));
            }
        }
    } else if (_managerVehicle->homePosition().isValid()) {
        settingsItem->setCoordinate(_managerVehicle->homePosition());
    }
}

int MissionController::resumeMissionIndex(void) const
{

    int resumeIndex = 0;

    if (!_editMode) {
        resumeIndex = _missionManager->lastCurrentIndex() + (_controllerVehicle->firmwarePlugin()->sendHomePositionToVehicle() ? 0 : 1);
        if (resumeIndex > 1 && resumeIndex != _visualItems->value<VisualMissionItem*>(_visualItems->count() - 1)->sequenceNumber()) {
            // Resume at the item previous to the item we were heading towards
            resumeIndex--;
        } else {
            resumeIndex = 0;
        }
    }

    return resumeIndex;
}

int MissionController::currentMissionIndex(void) const
{
    if (_editMode) {
        return -1;
    } else {
        int currentIndex = _missionManager->currentIndex();
        if (!_controllerVehicle->firmwarePlugin()->sendHomePositionToVehicle()) {
            currentIndex++;
        }
        return currentIndex;
    }
}

void MissionController::_currentMissionIndexChanged(int sequenceNumber)
{
    if (!_editMode) {
        if (!_controllerVehicle->firmwarePlugin()->sendHomePositionToVehicle()) {
            sequenceNumber++;
        }

        for (int i=0; i<_visualItems->count(); i++) {
            VisualMissionItem* item = qobject_cast<VisualMissionItem*>(_visualItems->get(i));
            item->setIsCurrentItem(item->sequenceNumber() == sequenceNumber);
        }
        emit currentMissionIndexChanged(currentMissionIndex());
    }
}

bool MissionController::syncInProgress(void) const
{
    return _missionManager->inProgress();
}

bool MissionController::dirty(void) const
{
    return _visualItems ? _visualItems->dirty() : false;
}


void MissionController::setDirty(bool dirty)
{
    if (_visualItems) {
        _visualItems->setDirty(dirty);
    }
}

void MissionController::_scanForAdditionalSettings(QmlObjectListModel* visualItems, Vehicle* vehicle)
{
    if (_editMode) {
        // First we look for a Fixed Wing Landing Pattern which is at the end
        FixedWingLandingComplexItem::scanForItem(visualItems, vehicle);
    }

    int scanIndex = 0;
    while (scanIndex < visualItems->count()) {
        VisualMissionItem* visualItem = visualItems->value<VisualMissionItem*>(scanIndex);

        qCDebug(MissionControllerLog) << "MissionController::_scanForAdditionalSettings count:scanIndex" << visualItems->count() << scanIndex;

        if (_editMode) {
            MissionSettingsItem* settingsItem = qobject_cast<MissionSettingsItem*>(visualItem);
            if (settingsItem) {
                scanIndex++;
                settingsItem->scanForMissionSettings(visualItems, scanIndex);
                continue;
            }
        }

        SimpleMissionItem* simpleItem = qobject_cast<SimpleMissionItem*>(visualItem);
        if (simpleItem) {
            scanIndex++;
            simpleItem->scanForSections(visualItems, scanIndex, vehicle);
        } else {
            // Complex item, can't have sections
            scanIndex++;
        }
    }
}

void MissionController::_updateContainsItems(void)
{
    emit containsItemsChanged(containsItems());
}

bool MissionController::containsItems(void) const
{
    return _visualItems ? _visualItems->count() > 1 : false;
}

void MissionController::removeAllFromVehicle(void)
{
    if (_masterController->offline()) {
        qCWarning(MissionControllerLog) << "MissionControllerLog::removeAllFromVehicle called while offline";
    } else if (syncInProgress()) {
        qCWarning(MissionControllerLog) << "MissionControllerLog::removeAllFromVehicle called while syncInProgress";
    } else {
        _itemsRequested = true;
        _missionManager->removeAll();
    }
}

QStringList MissionController::complexMissionItemNames(void) const
{
    QStringList complexItems;

    complexItems.append(_surveyMissionItemName);
    if (_controllerVehicle->fixedWing()) {
        complexItems.append(_fwLandingMissionItemName);
    }
    if (_controllerVehicle->multiRotor() || _controllerVehicle->vtol()) {
        complexItems.append(_structureScanMissionItemName);
    }

    return complexItems;
}

void MissionController::resumeMission(int resumeIndex)
{
    if (!_controllerVehicle->firmwarePlugin()->sendHomePositionToVehicle()) {
        resumeIndex--;
    }
    _missionManager->generateResumeMission(resumeIndex);
}

QGeoCoordinate MissionController::plannedHomePosition(void) const
{
    if (_settingsItem) {
        return _settingsItem->coordinate();
    } else {
        return QGeoCoordinate();
    }
}

void MissionController::applyDefaultMissionAltitude(void)
{
    double defaultAltitude = _appSettings->defaultMissionItemAltitude()->rawValue().toDouble();

    for (int i=1; i<_visualItems->count(); i++) {
        VisualMissionItem* item = _visualItems->value<VisualMissionItem*>(i);
        item->applyNewAltitude(defaultAltitude);
    }
}

void MissionController::_progressPctChanged(double progressPct)
{
    if (!qFuzzyCompare(progressPct, _progressPct)) {
        _progressPct = progressPct;
        emit progressPctChanged(progressPct);
    }
}

void MissionController::_visualItemsDirtyChanged(bool dirty)
{
    // We could connect signal to signal and not need this but this is handy for setting a breakpoint on
    emit dirtyChanged(dirty);
}

bool MissionController::showPlanFromManagerVehicle (void)
{
    qCDebug(MissionControllerLog) << "showPlanFromManagerVehicle" << _editMode;
    if (_masterController->offline()) {
        qCWarning(MissionControllerLog) << "MissionController::showPlanFromManagerVehicle called while offline";
        return true;    // stops further propagation of showPlanFromManagerVehicle due to error
    } else {
        if (!_managerVehicle->initialPlanRequestComplete()) {
            // The vehicle hasn't completed initial load, we can just wait for newMissionItemsAvailable to be signalled automatically
            qCDebug(MissionControllerLog) << "showPlanFromManagerVehicle: !initialPlanRequestComplete, wait for signal";
            return true;
        } else if (syncInProgress()) {
            // If the sync is already in progress, newMissionItemsAvailable will be signalled automatically when it is done. So no need to do anything.
            qCDebug(MissionControllerLog) << "showPlanFromManagerVehicle: syncInProgress wait for signal";
            return true;
        } else {
            // Fake a _newMissionItemsAvailable with the current items
            qCDebug(MissionControllerLog) << "showPlanFromManagerVehicle: sync complete simulate signal";
            _itemsRequested = true;
            _newMissionItemsAvailableFromVehicle(false /* removeAllRequested */);
            return false;
        }
    }
}

void MissionController::_managerSendComplete(bool error)
{
    // Fly view always reloads on send complete
    if (!error && !_editMode) {
        showPlanFromManagerVehicle();
    }
}

void MissionController::_managerRemoveAllComplete(bool error)
{
    if (!error) {
        // Remove all from vehicle so we always update
        showPlanFromManagerVehicle();
    }
}

int MissionController::currentPlanViewIndex(void) const
{
    return _currentPlanViewIndex;
}

VisualMissionItem* MissionController::currentPlanViewItem(void) const
{
    return _currentPlanViewItem;
}

void MissionController::setCurrentPlanViewIndex(int sequenceNumber, bool force)
{
    if(_visualItems && (force || sequenceNumber != _currentPlanViewIndex)) {
        _currentPlanViewItem  = NULL;
        _currentPlanViewIndex = -1;
        for (int i = 0; i < _visualItems->count(); i++) {
            VisualMissionItem* pVI = qobject_cast<VisualMissionItem*>(_visualItems->get(i));
            if (pVI && pVI->sequenceNumber() == sequenceNumber) {
                pVI->setIsCurrentItem(true);
                _currentPlanViewItem  = pVI;
                _currentPlanViewIndex = sequenceNumber;
            } else {
                pVI->setIsCurrentItem(false);
            }
        }
        emit currentPlanViewIndexChanged();
        emit currentPlanViewItemChanged();
    }
}

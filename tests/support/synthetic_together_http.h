#pragma once
#include "domain/music/together_types.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
namespace cirrus::test
{
inline QList<domain::TogetherAction> togetherHttpActions()
{
    using namespace domain;
    TogetherRoomId room{QStringLiteral("synthetic-room")};
    return {
        TogetherCreate{},
        TogetherCheck{room},
        TogetherJoin{room, QStringLiteral("102")},
        TogetherStatus{},
        TogetherEndCheck{room},
        TogetherEnd{room},
        TogetherHeartbeat{room,
                          {QStringLiteral("9007199254740993")},
                          TogetherPlayStatus::Play,
                          3000,
                          {{QStringLiteral("101"), 2}, {QStringLiteral("102"), 0}}},
        TogetherPlay{room,
                     TogetherPlayAction::Progress,
                     {QStringLiteral("9007199254740993")},
                     {QStringLiteral("2")},
                     TogetherPlayStatus::Pause,
                     3000,
                     6},
        TogetherQueue{room,
                      {{QStringLiteral("3")},
                       1,
                       TogetherPlayMode::Random,
                       {{QStringLiteral("2")}, {QStringLiteral("3")}},
                       {{QStringLiteral("3")}, {QStringLiteral("2")}}}},
        TogetherReplaceQueue{room,
                             {{QStringLiteral("2")},
                              0,
                              TogetherPlayMode::OrderLoop,
                              {{QStringLiteral("2")}, {QStringLiteral("2")}, {QStringLiteral("3")}},
                              {}},
                             {{QStringLiteral("101"), 9}, {QStringLiteral("102"), 4}},
                             6},
        TogetherReconnectInfo{},
        TogetherDeviceNotice{room},
        TogetherSyncNotice{room}};
}
inline QJsonObject togetherHttpObject(domain::TogetherKind kind)
{
    using namespace domain;
    QJsonObject data;
    const QJsonObject room{
        {"roomId", "synthetic-room"},
        {"chatRoomId", "synthetic-chat"},
        {"imType", "SELFHOST"},
        {"roomType", "FRIEND"},
        {"mainDevice", false},
        {"roomCanJoin", true},
        {"roomEnd", false},
        {"creatorId", 102},
        {"roomUsers", QJsonArray{QJsonObject{{"userId", 102},
                                             {"nickname", "synthetic"},
                                             {"avatarUrl", "https://p1.music.126.net/a.jpg"}},
                                 QJsonObject{{"userId", 101}, {"nickname", 7}}}}};
    switch (kind) {
    case TogetherKind::Create:
    case TogetherKind::Join:
        data = {{"roomInfo", room}, {"type", "NEW_ROOM"}, {"hintText", "synthetic hint"}};
        break;
    case TogetherKind::Status:
        data = {{"inRoom", true}, {"roomInfo", room}};
        break;
    case TogetherKind::Check:
        data = {{"joinable", false}, {"status", "synthetic-decision"}};
        break;
    case TogetherKind::Heartbeat:
        data = {{"result", true},
                {"type", "SUCCESS"},
                {"time", qint64(1795325693218)},
                {"timeSpan", 30}};
        break;
    case TogetherKind::EndCheck:
    case TogetherKind::Play:
    case TogetherKind::ReplaceQueue:
        data = {{"result", true}};
        break;
    case TogetherKind::Queue:
        // The observed shape (2026-11-22): data is {playCommand, playlist, resourceType}.
        data = {{"playCommand", QJsonObject{{"anotherOuterId", QJsonValue()},
                                            {"anotherUid", 103},
                                            {"commandType", "PROGRESS"},
                                            {"formerSongId", "9007199254740993"},
                                            {"outerId", QJsonValue()},
                                            {"targetSongId", "2"},
                                            {"progress", 3000},
                                            {"playStatus", "PAUSE"},
                                            {"clientSeq", 6},
                                            {"serverSeq", 88},
                                            {"resourceType", QJsonValue()},
                                            {"triggerType", "MANUAL"},
                                            {"userId", 102}}},
                {"playlist",
                 QJsonObject{{"displayList", QJsonObject{{"changed", true},
                                                         {"result", QJsonArray{"2", "3"}},
                                                         {"rcmdSongIds", QJsonArray{0}}}},
                             {"randomList", QJsonValue()},
                             {"playMode", "ORDER_LOOP"},
                             {"listMode", ""},
                             {"listModeParam", QJsonValue()},
                             {"replace", true},
                             {"songIdWithAlgList", QJsonValue()},
                             {"version",
                              QJsonArray{QJsonObject{
                                  {"outerId", QJsonValue()}, {"userId", 102}, {"version", 4}}}}}},
                {"resourceType", QJsonValue()}};
        break;
    case TogetherKind::ReconnectInfo:
        data = {{"canReconnect", true},
                {"roomId", "synthetic-room"},
                {"mainDevice", false},
                {"needDeviceReconnectConfirm", true},
                {"supportMultiDeviceReconnect", true}};
        break;
    case TogetherKind::End:
    case TogetherKind::DeviceNotice:
    case TogetherKind::SyncNotice:
        break;
    }
    return {{"code", 200}, {"data", data}};
}
inline QByteArray togetherHttpFixture(domain::TogetherKind kind)
{
    return QJsonDocument(togetherHttpObject(kind)).toJson(QJsonDocument::Compact);
}
} // namespace cirrus::test

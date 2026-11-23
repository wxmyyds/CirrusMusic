#pragma once
#include <QMetaType>
#include <QString>

namespace cirrus::domain
{
enum class BiliAccountStatus { LoggedOut, LoggedIn, Checking, InvalidCookie, NetworkError, StorageError };
struct BiliAccountState {
    BiliAccountStatus status = BiliAccountStatus::LoggedOut;
    QString name;
    friend bool operator==(const BiliAccountState&, const BiliAccountState&) = default;
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::BiliAccountState)
